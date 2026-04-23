/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNavigationModule.cpp
 * @brief   Module startup + debug toggle.
 *
 *          `SeinARTS.Debug.ShowNavigation [0|1|on|off]` toggles UE's per-
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
namespace
{
	IConsoleCommand* GShowNavCmd = nullptr;
	FTSTicker::FDelegateHandle GTickHandle;

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
		UE_LOG(LogTemp, Log, TEXT("SeinARTS.Debug.ShowNavigation = %s (ShowFlags.Navigation)"),
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
		if (!IsNavigationShowFlagOn()) return true;

		// Cells are scene-proxy-driven (USeinNavDebugComponent). Ticker only
		// handles per-active-move path viz — cheap, frame-lifetime.
		for (TObjectIterator<USeinNavigationSubsystem> It; It; ++It)
		{
			USeinNavigationSubsystem* Sub = *It;
			if (!IsValid(Sub)) continue;
			if (UWorld* World = Sub->GetWorld())
			{
				DrawActiveMoveDebug(World);
			}
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
			TEXT("SeinARTS.Debug.ShowNavigation"),
			TEXT("Toggle ShowFlags.Navigation across all viewports (same bit as UE's 'P' hotkey and `showflag.navigation`). Drives USeinNavDebugComponent cell viz + per-action path highlights. Usage: SeinARTS.Debug.ShowNavigation [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowNavigationCommand),
			ECVF_Default);
	}

	GTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&DebugDrawTick), 0.0f);
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
	FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);
#endif
}
