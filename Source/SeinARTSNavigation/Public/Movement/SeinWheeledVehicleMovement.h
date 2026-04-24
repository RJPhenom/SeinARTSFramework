/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.h
 * @brief   Wheeled-vehicle movement — turn-while-moving, no in-place pivot.
 *
 *          A wheeled vehicle steers by moving; stopping means you can't turn.
 *          Motion this tick is always along the current forward vector (at
 *          MoveSpeed), with the facing rotated toward the next waypoint by
 *          at most TurnRate × DeltaTime. Emergent behavior: tight turns
 *          force the vehicle into wider arcs; a gentle target is reached
 *          mostly straight.
 *
 *          Does NOT model a hard minimum turn radius — turn rate alone
 *          produces a speed-dependent arc that approximates vehicle
 *          steering without extra state. Call that a feature not a bug:
 *          fewer knobs, fewer ways for designers to tune into a dead end.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinLocomotion.h"
#include "SeinWheeledVehicleMovement.generated.h"

UCLASS(meta = (DisplayName = "Wheeled Vehicle"))
class SEINARTSNAVIGATION_API USeinWheeledVehicleMovement : public USeinLocomotion
{
	GENERATED_BODY()

public:
	virtual bool Tick(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveData,
		const FSeinPath& Path,
		int32& CurrentWaypointIndex,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		USeinNavigation* Nav) override;
};
