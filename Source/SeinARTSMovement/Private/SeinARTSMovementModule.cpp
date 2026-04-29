/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementModule.cpp
 * @brief   Module startup + movement debug toggles.
 *
 *          `Sein.Nav.Show.SteeringVectors [0|1|on|off]` toggles a custom show
 *          flag (`ShowFlags.SeinSteeringVectors`) that gates each unit's
 *          per-tick steering bias arrows. Movement code consumes the flag
 *          via `UE::SeinARTSMovement::IsSteeringVectorsShowFlagOn`.
 *
 *          The active-move path overlay ticker iterates running
 *          `USeinMoveToAction` instances and draws yellow path cells +
 *          waypoint lines + the current target marker. Gates per-world on
 *          `UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld` so a
 *          single `Sein.Nav.Show` toggle drives both the cell-quad scene
 *          proxy (Nav module's `USeinNavDebugComponent`) and this overlay —
 *          designers see one unified UX even though the rendering work is
 *          split across two modules.
 *
 *          Shipping strip: ticker, console command, and helper draw functions
 *          are gated on UE_ENABLE_DEBUG_DRAWING. In UE_BUILD_SHIPPING the
 *          module's StartupModule / ShutdownModule are no-ops.
 */

#include "SeinARTSMovementModule.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Actions/SeinMoveToAction.h"
#include "SeinARTSNavigationModule.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"

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

IMPLEMENT_MODULE(FSeinARTSMovementModule, SeinARTSMovement)

#if UE_ENABLE_DEBUG_DRAWING
// Custom show flag for the avoidance steering bias arrows. UE doesn't ship a
// matching built-in, so we register one via TCustomShowFlag (same pattern as
// SeinARTSFogOfWar). The flag drives per-tick arrow draws inside
// USeinMovement::ComputeAvoidanceVector — module-public so the static
// `IsSteeringVectorsShowFlagOn` query can be linked from the movement TU.
namespace UE::SeinARTSMovement
{
	static TCustomShowFlag<> ShowSteeringVectors(
		TEXT("SeinSteeringVectors"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSMovement", "ShowSteeringVectors", "Sein Steering Vectors"));

	bool IsSteeringVectorsShowFlagOnForWorld(UWorld* World)
	{
		if (!World) return false;

#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->GetWorld() == World
				    && ShowSteeringVectors.IsEnabled(Vp->EngineShowFlags))
				{
					return true;
				}
			}
		}
#endif
		// Enumerate every FWorldContext's GameViewport — in multi-client PIE
		// there are multiple GameViewports and only one is GEngine->GameViewport.
		// Match by world so toggling the flag off on the user's visible
		// viewport actually hides the arrows for that viewport's units, even
		// if some other viewport (editor / other PIE) still has the flag on.
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport
				    && Ctx.GameViewport->GetWorld() == World
				    && ShowSteeringVectors.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}
}

namespace
{
	IConsoleCommand* GShowSteeringCmd = nullptr;
	FTSTicker::FDelegateHandle GTickHandle;

	/** Set ShowFlags.SeinSteeringVectors across all editor + game viewport
	 *  clients. Iterates every FWorldContext's GameViewport so multi-client
	 *  PIE applies the toggle to every PIE window, not just the primary one
	 *  pointed at by GEngine->GameViewport. */
	static void SetSteeringVectorsShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSMovement::ShowSteeringVectors.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport)
				{
					UE::SeinARTSMovement::ShowSteeringVectors.SetEnabled(Ctx.GameViewport->EngineShowFlags, bEnable);
				}
			}
		}
	}

	/** "Any viewport has the flag" check used by the console-command toggle.
	 *  Public consumers (per-tick draw gate) use the per-world variant
	 *  `IsSteeringVectorsShowFlagOnForWorld` instead — only the toggle
	 *  command needs a global "current state" query. */
	static bool IsSteeringVectorsShowFlagOn_AnyViewport()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && UE::SeinARTSMovement::ShowSteeringVectors.IsEnabled(Vp->EngineShowFlags)) return true;
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport && UE::SeinARTSMovement::ShowSteeringVectors.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}

	static void OnShowSteeringVectorsCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		// Parse on/off the same way as ShowNavigation. No-args = toggle.
		bool bEnable = !IsSteeringVectorsShowFlagOn_AnyViewport();
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
		// Static cells AND dynamic blocker stamps are scene-proxy-driven from the
		// SeinARTSNavigation module (USeinNavDebugComponent rebuilds on
		// OnNavigationMutated). Ticker only handles per-active-move path viz —
		// that's per-unit ephemeral data that doesn't fold cleanly into the
		// global cell mesh. Showflag gate stays per-world to match the proxy's
		// GetViewRelevance behavior (no editor→PIE bleed).
		for (TObjectIterator<USeinNavigationSubsystem> It; It; ++It)
		{
			USeinNavigationSubsystem* Sub = *It;
			if (!IsValid(Sub)) continue;
			UWorld* World = Sub->GetWorld();
			if (!World) continue;
			if (!UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld(World)) continue;
			DrawActiveMoveDebug(World);
		}
		return true;
	}
}
#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSMovementModule::StartupModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!GShowSteeringCmd)
	{
		GShowSteeringCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show.SteeringVectors"),
			TEXT("Toggle ShowFlags.SeinSteeringVectors across all viewports (custom show flag, same UX as Sein.FogOfWar.Show). When on, each unit running an avoidance-enabled movement draws a magenta arrow each tick showing the current steering bias vector. Usage: Sein.Nav.Show.SteeringVectors [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowSteeringVectorsCommand),
			ECVF_Default);
	}

	GTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&DebugDrawTick), 0.0f);
#endif
}

void FSeinARTSMovementModule::ShutdownModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (GShowSteeringCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowSteeringCmd);
		GShowSteeringCmd = nullptr;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);
#endif
}
