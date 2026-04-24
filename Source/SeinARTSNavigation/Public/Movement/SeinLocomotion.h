/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLocomotion.h
 * @brief   Abstract base for per-unit locomotion modes.
 *
 *          USeinMoveToAction resolves a path via USeinNavigation, then
 *          delegates per-tick advancement to a subclass of this. Designers
 *          pick the locomotion per unit via FSeinMovementData::LocomotionClass;
 *          null defaults to USeinBasicMovement.
 *
 *          Shipped subclasses:
 *          - USeinBasicMovement            — direct seek + arrive (default)
 *          - USeinInfantryMovement         — basic + face-velocity + (future) avoidance
 *          - USeinWheeledVehicleMovement   — turn-while-moving, no in-place rotation
 *          - USeinTrackedVehicleMovement   — can rotate in place, slow accel
 *
 *          Locomotion instances are owned by the action (one per active move).
 *          Long-lived state (current waypoint index) lives on the action and
 *          is passed in by reference; transient per-instance state (facing
 *          interpolation, accel ramp) can live on the locomotion itself.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinLocomotion.generated.h"

class USeinNavigation;
struct FSeinEntity;
struct FSeinMovementData;
struct FSeinPath;

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Sein Locomotion"))
class SEINARTSNAVIGATION_API USeinLocomotion : public UObject
{
	GENERATED_BODY()

public:

	/** Called once when a MoveTo action resolves a path (before the first
	 *  Tick). Default: no-op. Override to initialize subclass state (e.g.,
	 *  wheeled vehicles caching the starting facing). */
	virtual void OnMoveBegin(FSeinEntity& Entity, const FSeinMovementData& MoveData, const FSeinPath& Path) {}

	/** Called each sim tick while the move is active. Mutates Entity.Transform
	 *  in place and advances CurrentWaypointIndex as waypoints are consumed.
	 *  @return true when the entity has reached the final waypoint. */
	virtual bool Tick(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveData,
		const FSeinPath& Path,
		int32& CurrentWaypointIndex,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		USeinNavigation* Nav) PURE_VIRTUAL(USeinLocomotion::Tick, return true;);

	/** Called when the action ends (completed/cancelled/failed). Default:
	 *  no-op. Override to clean up subclass transient state. */
	virtual void OnMoveEnd(FSeinEntity& Entity) {}

protected:

	/** Shortest signed angular delta from `From` to `To`, wrapped to [-π, π]. */
	static FFixedPoint ShortestAngleDelta(FFixedPoint From, FFixedPoint To);

	/** Clamp `Val` to [Min, Max]. */
	static FFixedPoint ClampFP(FFixedPoint Val, FFixedPoint Min, FFixedPoint Max);

	/** Build a yaw-only rotation quaternion (upright units, flat ground). */
	static FFixedQuaternion YawOnly(FFixedPoint YawRadians);

	/** Yaw in radians extracted from the forward vector of a rotation. Matches
	 *  atan2(forward.Y, forward.X) so it round-trips with YawOnly. */
	static FFixedPoint YawFromRotation(const FFixedQuaternion& Rotation);
};
