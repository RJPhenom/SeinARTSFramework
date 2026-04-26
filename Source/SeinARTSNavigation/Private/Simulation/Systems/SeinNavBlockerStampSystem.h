/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavBlockerStampSystem.h
 * @brief   PreTick system that walks entities carrying FSeinExtentsData with
 *          bBlocksNav = true, expands each entity's Shapes into
 *          FSeinDynamicBlocker entries, and pushes the flat list to the
 *          active USeinNavigation. Pathfinding inside this same tick sees
 *          the fresh stamps.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinExtentsData.h"
#include "Components/SeinMovementData.h"
#include "Stamping/SeinStampShape.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "SeinNavigation.h"
#include "Logging/LogMacros.h"

// Diagnostic log for the nav-blocker stamping pipeline. Off by default —
// flip on with `log LogSeinNavBlockerStamp Verbose` to confirm how many
// blockers each tick produces and which entities contributed. Use to
// diagnose "viz isn't showing" — if the count is 0, no entity is supplying
// blocker data; if it's >0, the issue is downstream (CollectDebugBlockerCells
// or DrawDynamicBlockersDebug).
DEFINE_LOG_CATEGORY_STATIC(LogSeinNavBlockerStamp, Log, All);

/**
 * System: Nav Blocker Stamp
 * Phase: PreTick | Priority: 7
 *
 * Runs after SpatialHash (priority 5) and before pathfinding (which happens
 * in AbilityExecution phase via MoveToAction's TickAction). Walks the entity
 * pool in handle-index order (deterministic — same as the spatial hash
 * rebuild), filters to entities whose FSeinExtentsData has `bBlocksNav` set,
 * expands each entity's Shapes into FSeinDynamicBlocker entries, and pushes
 * the flat list to the nav.
 *
 * Each blocker carries the owning entity's `BlockedNavLayerMask`; the
 * pathfinding overlay-rebuild gates per-agent via mask intersection, so
 * water blockers (mask = Default) ignore amphibious agents (mask = N0).
 *
 * Pure rebuild each tick — no diff tracking. Cheap at sim scale (typical
 * scenarios have a few dozen extents-bearing entities, most without
 * bBlocksNav set).
 */
class FSeinNavBlockerStampSystem final : public ISeinSystem
{
public:
	explicit FSeinNavBlockerStampSystem(USeinNavigation* InNav)
		: Nav(InNav) {}

	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		USeinNavigation* NavPtr = Nav.Get();
		if (!NavPtr) return;

		Blockers.Reset();

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FFixedVector EntityPos = Entity.Transform.GetLocation();
			const FFixedQuaternion EntityRot = Entity.Transform.Rotation;
			const FSeinExtentsData* Extents = World.GetComponent<FSeinExtentsData>(Handle);

			// Designer authored Extents → strict honoring of flags. If
			// bBlocksNav is false, do not stamp — falling back to the
			// movement footprint here would silently override the explicit
			// "this entity doesn't block paths" intent (infantry case).
			if (Extents)
			{
				if (!Extents->bBlocksNav) return;
				if (Extents->Shapes.Num() == 0) return;
				if (Extents->BlockedNavLayerMask == 0) return;

				for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
				{
					FSeinDynamicBlocker B;
					B.Shape = ExtShape.AsStampShape();
					B.EntityCenter = EntityPos;
					B.EntityRotation = EntityRot;
					B.Owner = Handle;
					B.BlockedNavLayerMask = Extents->BlockedNavLayerMask;
					Blockers.Add(B);
				}
				return;
			}

			// Fallback only when NO Extents authored — a vehicle BP that
			// declares a FootprintRadius for steering but never explicitly
			// authored Extents still gets sensible nav blocking. As soon
			// as the designer adds an Extents component, the flags above
			// take over (including "off" if they want it off).
			const FSeinMovementData* MoveData = World.GetComponent<FSeinMovementData>(Handle);
			if (!MoveData || MoveData->FootprintRadius <= FFixedPoint::Zero) return;

			FSeinStampShape Synthetic;
			Synthetic.Shape = ESeinStampShape::Radial;
			Synthetic.bEnabled = true;
			Synthetic.Radius = MoveData->FootprintRadius;

			FSeinDynamicBlocker B;
			B.Shape = Synthetic;
			B.EntityCenter = EntityPos;
			B.EntityRotation = EntityRot;
			B.Owner = Handle;
			B.BlockedNavLayerMask = 0x01; // Default layer
			Blockers.Add(B);
		});

		NavPtr->SetDynamicBlockers(Blockers);

		UE_LOG(LogSeinNavBlockerStamp, Verbose,
			TEXT("Stamped %d nav blocker(s) into nav (one per enabled extents shape, plus footprint fallbacks)"),
			Blockers.Num());
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 7; }
	virtual FName GetSystemName() const override { return TEXT("NavBlockerStamp"); }

private:
	TWeakObjectPtr<USeinNavigation> Nav;
	TArray<FSeinDynamicBlocker> Blockers;
};
