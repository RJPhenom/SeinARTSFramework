/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.h
 * @brief   Wheeled-vehicle locomotion — bicycle kinematics + look-ahead path tracking.
 *
 *          Steering is modeled as a single-axle bicycle: a steer angle clamped
 *          to ±MaxSteerAngle, smoothed toward its desired value at SteerResponse,
 *          producing a yaw rate of (speed / wheelbase) · tan(steer). This
 *          naturally couples turn radius to speed — slow turns tight, fast
 *          turns wide — and means a stationary vehicle cannot pivot in place.
 *
 *          The vehicle does NOT modulate throttle by alignment (unlike
 *          tracked); it always targets MaxSpeed and lets the bicycle model
 *          decide arc geometry. Look-ahead path tracking on the polyline
 *          prevents the "death spiral" where a vehicle orbits a waypoint it
 *          can't physically hit — the steering target moves with the unit's
 *          progress along the path, so the carrot is always reachable.
 *
 *          Reverse (auto + commanded) is deferred. Forward-only for MVP.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinLocomotion.h"
#include "Types/FixedPoint.h"
#include "SeinWheeledVehicleMovement.generated.h"

UCLASS(meta = (DisplayName = "Wheeled Vehicle"))
class SEINARTSNAVIGATION_API USeinWheeledVehicleMovement : public USeinLocomotion
{
	GENERATED_BODY()

public:

	USeinWheeledVehicleMovement();

	/** Distance between front and rear axles (world units). Smaller = tighter
	 *  minimum turn radius for a given speed. Affects feel substantially —
	 *  jeeps ~150-220, half-tracks ~300-400. */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "1.0"))
	FFixedPoint Wheelbase;

	/** Maximum steer angle in radians (±). Larger = tighter possible turns. */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "0.0"))
	FFixedPoint MaxSteerAngle;

	/** How fast the steer angle interpolates toward its desired value
	 *  (1/seconds). Higher = snappier wheels; lower = gradual lean-in. */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "0.1"))
	FFixedPoint SteerResponse;

	/** Look-ahead distance (world units) along the path for the steering
	 *  carrot point. */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance;

	/** Optional minimum-slowdown distance. Arrival braking is primarily
	 *  driven by KINEMATICS — `v_max = √(2·Deceleration·DistanceToFinal)` —
	 *  which auto-scales with the unit's speed and decel. This property
	 *  layers an additional LINEAR speed cap inside the configured distance,
	 *  applied only if it brakes harder than physics would. Use it for a
	 *  "cautious arrival" feel on units that should start visibly slowing
	 *  earlier than physics demands. Set to 0 for pure physics-based
	 *  braking (recommended default behavior). */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "0.0"))
	FFixedPoint ArrivalSlowdownDistance;

	/** Min throttle multiplier at full steer (1.0 = no slow-down on sharp
	 *  turns; 0.5 = halve speed at MaxSteerAngle). Falloff is quadratic by
	 *  |steer|/MaxSteer. Set 1.0 to disable. */
	UPROPERTY(EditAnywhere, Category = "Wheeled", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint TurnSpeedFloor;

	virtual void OnMoveBegin(const FSeinLocomotionContext& Ctx) override;
	virtual bool Tick(const FSeinLocomotionContext& Ctx) override;

protected:

	/** Per-instance current steer angle (radians, ± MaxSteerAngle). Smoothed
	 *  toward desired across ticks via SteerResponse. Resets per move action —
	 *  steer position depends on the active path's look-ahead, so carrying it
	 *  across orders provides no useful continuity. (Forward speed DOES carry,
	 *  but that lives on `FSeinMovementData::CurrentSpeed`.) */
	FFixedPoint CurrentSteer;

	/** Latched-at-OnMoveBegin reverse decision. When true, the vehicle drives
	 *  backward to reach the destination — desired yaw flips to "back faces
	 *  goal," steer command inverts to handle bicycle reverse-kinematics
	 *  (a steered front wheel arcs the rear in the opposite direction when
	 *  the vehicle moves backward), and target speed becomes negative. */
	bool bIsReversing = false;
};
