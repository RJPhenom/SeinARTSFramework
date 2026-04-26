/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSpatialHashSystem.h
 * @brief   PreTick system that rebuilds the world's spatial hash from current
 *          entity positions. Foundation for avoidance, proximity queries, and
 *          any future cross-cutting "find me units near here" logic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinSpatialHash.h"
#include "Components/SeinMovementData.h"

/**
 * System: Spatial Hash Rebuild
 * Phase: PreTick | Priority: 5
 *
 * Runs after EffectTick (priority 0) so any tick-time spawn/destroy from
 * effects has already settled. Walks the entity pool in handle-index order,
 * inserts every entity that has a FSeinMovementData with FootprintRadius > 0
 * into the hash. Entities without movement data, dead entities, and
 * intangible entities (FootprintRadius == 0) are skipped.
 *
 * Full clear+rebuild each tick. Cheap at sim scale (a few thousand inserts)
 * and obviates any state-tracking. Incremental updates are a Phase E
 * optimization in the avoidance plan.
 */
class FSeinSpatialHashSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		FSeinSpatialHash& Hash = World.GetSpatialHash();
		Hash.Clear();

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FSeinMovementData* MoveData = World.GetComponent<FSeinMovementData>(Handle);
			if (!MoveData) return;
			if (MoveData->FootprintRadius <= FFixedPoint::Zero) return;
			Hash.Insert(Handle, Entity.Transform.GetLocation());
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 5; }
	virtual FName GetSystemName() const override { return TEXT("SpatialHash"); }
};
