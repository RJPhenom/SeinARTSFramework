/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNavigationModule.cpp
 * @brief   Module startup + debug toggle.
 *
 *          `Sein.Nav.Show [0|1|on|off]` toggles UE's per-
 *          viewport `ShowFlags.Navigation` flag (same bit as the 'P' hotkey
 *          and `showflag.navigation`). That flag drives:
 *            - Cell viz via `USeinNavDebugComponent`'s scene proxy
 *              (`GetViewRelevance` gates on `EngineShowFlags.Navigation`)
 *            - Per-active-move path cell highlights + waypoint lines
 *              (drawn by this module's ticker)
 *
 *          Shipping strip: the ticker registration, console command, and all
 *          helper draw functions are gated on UE_ENABLE_DEBUG_DRAWING. In
 *          UE_BUILD_SHIPPING the module's StartupModule / ShutdownModule are
 *          no-ops, no ticker is scheduled, no console command is registered.
 */

#include "SeinARTSNavigationModule.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Actions/SeinMoveToAction.h"
#include "Debug/SeinNavDebugComponent.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "ShowFlags.h"
#include "UObject/UObjectIterator.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSNavigationModule, SeinARTSNavigation)

#if UE_ENABLE_DEBUG_DRAWING
// Custom show flag for the avoidance steering bias arrows. UE doesn't ship a
// matching built-in, so we register one via TCustomShowFlag (same pattern as
// SeinARTSFogOfWar). The flag drives per-tick arrow draws inside
// USeinLocomotion::ComputeAvoidanceVector — module-public so the static
// `IsSteeringVectorsShowFlagOn` query can be linked from the locomotion TU.
namespace UE::SeinARTSNavigation
{
	static TCustomShowFlag<> ShowSteeringVectors(
		TEXT("SeinSteeringVectors"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSNavigation", "ShowSteeringVectors", "Sein Steering Vectors"));

	// Layer override for the nav debug viz. -1 = no override (every blocker
	// renders). 0..7 = pinned layer bit; CollectDebugBlockerCells filters to
	// blockers whose BlockedNavLayerMask has that bit set. Lets a designer
	// audit "what blocks my Amphibious unit?" by selecting its layer bit.
	// Storage lives in the named namespace (not anonymous) so the public
	// TryGetDebugNavLayerOverride helper below can read it.
	int32 GDebugNavLayerOverride = -1;

	bool IsSteeringVectorsShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && ShowSteeringVectors.IsEnabled(Vp->EngineShowFlags)) return true;
			}
		}
#endif
		if (GEngine && GEngine->GameViewport &&
			ShowSteeringVectors.IsEnabled(GEngine->GameViewport->EngineShowFlags))
		{
			return true;
		}
		return false;
	}

	bool TryGetDebugNavLayerOverride(int32& OutBitIndex)
	{
		if (GDebugNavLayerOverride >= 0 && GDebugNavLayerOverride <= 7)
		{
			OutBitIndex = GDebugNavLayerOverride;
			return true;
		}
		return false;
	}
}

namespace
{
	IConsoleCommand* GShowNavCmd = nullptr;
	IConsoleCommand* GShowSteeringCmd = nullptr;
	IConsoleCommand* GLayerPerspectiveCmd = nullptr;
	FTSTicker::FDelegateHandle GTickHandle;

	// Console-command intent for the Navigation showflag. Console toggles set
	// this AND the live viewport flags. The PIE-start hook reads this when a
	// new game viewport spins up so toggling Nav-on before clicking Play
	// carries through into PIE — the game viewport doesn't exist at toggle
	// time, so the live-viewport set in SetNavigationShowFlag has no effect
	// on it. Without the intent + hook, designers see nothing in PIE despite
	// stamping working correctly.
	bool GShowNavigationIntent = false;
#if WITH_EDITOR
	FDelegateHandle GPostPIEStartedHandle;
#endif

	static void SetNavigationShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					Vp->EngineShowFlags.SetNavigation(bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->EngineShowFlags.SetNavigation(bEnable);
		}

		// Persist intent so a future PIE startup picks it up even though
		// the GameViewport above didn't exist at toggle time.
		GShowNavigationIntent = bEnable;
	}

	static bool IsNavigationShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->EngineShowFlags.Navigation) return true;
			}
		}
#endif
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->EngineShowFlags.Navigation)
		{
			return true;
		}
		return false;
	}

	/** Per-world variant for the tick draw. STRICT per-viewport gating —
	 *  PIE / standalone consult the game viewport; editor consults editor
	 *  viewports rendering the matching world. No cross-pollination: the
	 *  designer toggles each independently. The PIE-start hook below
	 *  carries the console-command intent into the freshly-created game
	 *  viewport so toggling-before-Play still works. */
	static bool IsNavigationShowFlagOnForWorld(UWorld* World)
	{
		if (!World) return false;

		if (World->IsGameWorld())
		{
			return GEngine && GEngine->GameViewport
			    && GEngine->GameViewport->GetWorld() == World
			    && GEngine->GameViewport->EngineShowFlags.Navigation;
		}

#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (!Vp || Vp->GetWorld() != World) continue;
				if (Vp->EngineShowFlags.Navigation) return true;
			}
		}
#endif
		return false;
	}

	static void OnShowNavigationCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsNavigationShowFlagOn();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetNavigationShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.Nav.Show = %s (ShowFlags.Navigation)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}

	/** Set ShowFlags.SeinSteeringVectors across all editor + game viewport clients. */
	static void SetSteeringVectorsShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSNavigation::ShowSteeringVectors.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport)
		{
			UE::SeinARTSNavigation::ShowSteeringVectors.SetEnabled(GEngine->GameViewport->EngineShowFlags, bEnable);
		}
	}

	static void OnShowSteeringVectorsCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		// Parse on/off the same way as ShowNavigation. No-args = toggle.
		bool bEnable = !UE::SeinARTSNavigation::IsSteeringVectorsShowFlagOn();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetSteeringVectorsShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.Nav.Show.SteeringVectors = %s (ShowFlags.SeinSteeringVectors)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}

	/** Force every nav debug proxy to rebuild — picks up the new layer
	 *  filter immediately instead of waiting for the next blocker mutation. */
	static void MarkAllNavDebugProxiesDirty()
	{
		for (TObjectIterator<USeinNavDebugComponent> It; It; ++It)
		{
			if (IsValid(*It))
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	static void OnLayerPerspectiveCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		if (Args.Num() == 0)
		{
			UE::SeinARTSNavigation::GDebugNavLayerOverride = -1;
			UE_LOG(LogTemp, Log, TEXT("Sein.Nav.Show.Layer = ALL (reset)"));
			MarkAllNavDebugProxiesDirty();
			return;
		}
		const int32 Parsed = FCString::Atoi(*Args[0]);
		if (Parsed < 0 || Parsed > 7)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Sein.Nav.Show.Layer: expected 0-7 (0=Default, 1..7=N0..N6), got %s"),
				*Args[0]);
			return;
		}
		UE::SeinARTSNavigation::GDebugNavLayerOverride = Parsed;
		static const TCHAR* LayerNames[] = {
			TEXT("Default"),
			TEXT("N0"), TEXT("N1"), TEXT("N2"), TEXT("N3"), TEXT("N4"), TEXT("N5"), TEXT("N6")
		};
		UE_LOG(LogTemp, Log,
			TEXT("Sein.Nav.Show.Layer = bit %d (%s). Use no-args to reset to ALL."),
			Parsed, LayerNames[Parsed]);
		MarkAllNavDebugProxiesDirty();
	}

	/** Draw active-move debug for `World`:
	 *   - Yellow solid boxes at each cell the remaining path crosses
	 *   - Blue solid box at the final-destination cell (flag marker)
	 *   - Yellow line overlay from entity → current target → remaining waypoints
	 *   - Sphere at the destination, white dots at each waypoint
	 *
	 *  All lifetime=0 (one frame) so nothing accumulates. Draws only while
	 *  the Navigation show flag is on. */
	static void DrawActiveMoveDebug(UWorld* World)
	{
		if (!World) return;
		USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavSub = World->GetSubsystem<USeinNavigationSubsystem>();
		if (!Sim || !NavSub) return;
		USeinNavigation* Nav = NavSub->GetNavigation();
		if (!Nav) return;

		// Path = yellow, final destination = blue (RTS "flag on the map" feel).
		// More opaque than the translucent green floor tint so the path pops
		// clearly against baked cells.
		const FColor CellRemaining(255, 220, 0, 220);
		const FColor CellTarget(0, 140, 255, 230);
		const FColor LineEntityToTarget(255, 220, 0);
		const FColor LineChain(255, 220, 0);

		for (TObjectIterator<USeinMoveToAction> It; It; ++It)
		{
			USeinMoveToAction* Action = *It;
			// NOTE: don't filter by Action->GetWorld() — the proxy's outer is the
			// transient package, so the action's world chain is null. Use the
			// entity-lookup below as the implicit world filter (Sim->GetEntity
			// only returns valid for entities registered in THIS sim).
			if (!IsValid(Action) || !Action->IsPathValid()) continue;
			// Skip actions that have finished — they linger as unreferenced
			// UObjects with a still-valid Path until the next GC cycle, and
			// without this filter every move order leaves a trail behind.
			if (Action->bCompleted || Action->bCancelled || Action->bFailed) continue;

			const FSeinPath& Path = Action->Path;
			const int32 CurIdx = Action->GetCurrentWaypointIndex();
			if (!Path.Waypoints.IsValidIndex(CurIdx)) continue;

			const FSeinEntity* E = Sim->GetEntity(Action->OwnerEntity);
			if (!E) continue;

			const FFixedVector AgentPosFixed = E->Transform.GetLocation();

			// Path cell highlights: yellow cells along the remaining path, blue cell
			// at the final destination (flag marker).
			TArray<FVector> RemainingCells;
			TArray<FVector> TargetCell;
			float HalfExtent = 0.0f;
			Nav->CollectDebugPathCells(AgentPosFixed, Path.Waypoints, CurIdx,
				RemainingCells, TargetCell, HalfExtent);

			if (HalfExtent > 0.0f)
			{
				// Lift path cells well above the scene-proxy floor tint (which
				// draws at CellHeight + 2cm) so they don't z-fight / alpha-blend
				// into invisibility. 15cm center + 5cm half-height = box spans
				// +10 to +20 above the cell's baked height.
				const FVector Ext(HalfExtent * 0.95f, HalfExtent * 0.95f, 5.0f);
				for (const FVector& Center : RemainingCells)
				{
					DrawDebugSolidBox(World, Center + FVector(0, 0, 15.0f), Ext, CellRemaining, false, 0.0f, SDPG_World);
				}
				for (const FVector& Center : TargetCell)
				{
					DrawDebugSolidBox(World, Center + FVector(0, 0, 16.0f), Ext * 1.05f, CellTarget, false, 0.0f, SDPG_World);
				}
			}

			// Waypoint line overlay.
			auto ToFVector = [](const FFixedVector& V, float ZBias = 8.0f)
			{
				return FVector(V.X.ToFloat(), V.Y.ToFloat(), V.Z.ToFloat() + ZBias);
			};

			const FVector EntityPos = ToFVector(AgentPosFixed);
			const FVector CurTarget = ToFVector(Path.Waypoints[CurIdx]);

			DrawDebugLine(World, EntityPos, CurTarget, LineEntityToTarget, false, 0.0f, 0, 3.0f);
			for (int32 i = CurIdx; i < Path.Waypoints.Num() - 1; ++i)
			{
				DrawDebugLine(World, ToFVector(Path.Waypoints[i]), ToFVector(Path.Waypoints[i + 1]),
					LineChain, false, 0.0f, 0, 2.0f);
			}
			DrawDebugSphere(World, ToFVector(Path.Waypoints.Last()), 20.0f, 12,
				LineChain, false, 0.0f, 0, 2.0f);
			for (int32 i = CurIdx; i < Path.Waypoints.Num(); ++i)
			{
				DrawDebugPoint(World, ToFVector(Path.Waypoints[i]), 6.0f, FColor::White, false, 0.0f);
			}
		}
	}

	static bool DebugDrawTick(float /*DeltaTime*/)
	{
		// Static cells AND dynamic blocker stamps are scene-proxy-driven now
		// (USeinNavDebugComponent rebuilds on OnNavigationMutated; the nav
		// broadcasts that delegate from SetDynamicBlockers when the blocker
		// fingerprint changes). Ticker only handles the per-active-move path
		// viz — that's per-unit ephemeral data that doesn't fold cleanly
		// into the global cell mesh. Showflag gate stays per-world to match
		// the proxy's GetViewRelevance behavior (no editor→PIE bleed).
		for (TObjectIterator<USeinNavigationSubsystem> It; It; ++It)
		{
			USeinNavigationSubsystem* Sub = *It;
			if (!IsValid(Sub)) continue;
			UWorld* World = Sub->GetWorld();
			if (!World) continue;
			if (!IsNavigationShowFlagOnForWorld(World)) continue;
			DrawActiveMoveDebug(World);
		}
		return true;
	}
}
#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSNavigationModule::StartupModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!GShowNavCmd)
	{
		GShowNavCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show"),
			TEXT("Toggle ShowFlags.Navigation across all viewports (same bit as UE's 'P' hotkey and `showflag.navigation`). Drives USeinNavDebugComponent cell viz + per-action path highlights. Usage: Sein.Nav.Show [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowNavigationCommand),
			ECVF_Default);
	}
	if (!GShowSteeringCmd)
	{
		GShowSteeringCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show.SteeringVectors"),
			TEXT("Toggle ShowFlags.SeinSteeringVectors across all viewports (custom show flag, same UX as Sein.FogOfWar.Show). When on, each unit running an avoidance-enabled locomotion draws a magenta arrow each tick showing the current steering bias vector. Usage: Sein.Nav.Show.SteeringVectors [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowSteeringVectorsCommand),
			ECVF_Default);
	}
	if (!GLayerPerspectiveCmd)
	{
		GLayerPerspectiveCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show.Layer"),
			TEXT("Filter the nav debug viewer to render only blockers that affect a specific agent layer bit. Usage: Sein.Nav.Show.Layer <bit 0-7>. 0 = Default, 1..7 = N0..N6 (names from NavLayers settings). No args → reset to ALL (every blocker rendered). Useful for auditing 'what blocks my Amphibious unit?' by selecting that layer's bit."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnLayerPerspectiveCommand),
			ECVF_Default);
	}

	GTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&DebugDrawTick), 0.0f);

#if WITH_EDITOR
	// PIE-start hook: when a fresh PIE session spins up, the GameViewport
	// has just been created and any console-toggled Nav showflag intent
	// hasn't reached it yet (the toggle ran when GameViewport was null).
	// Apply the stored intent here so toggling Nav-on in the editor before
	// hitting Play correctly carries through into PIE.
	GPostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda(
		[](const bool /*bIsSimulating*/)
		{
			if (GShowNavigationIntent && GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->EngineShowFlags.SetNavigation(true);
			}
		});
#endif
#endif
}

void FSeinARTSNavigationModule::ShutdownModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (GShowNavCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowNavCmd);
		GShowNavCmd = nullptr;
	}
	if (GShowSteeringCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowSteeringCmd);
		GShowSteeringCmd = nullptr;
	}
	if (GLayerPerspectiveCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GLayerPerspectiveCmd);
		GLayerPerspectiveCmd = nullptr;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);

#if WITH_EDITOR
	if (GPostPIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(GPostPIEStartedHandle);
		GPostPIEStartedHandle.Reset();
	}
#endif
#endif
}
