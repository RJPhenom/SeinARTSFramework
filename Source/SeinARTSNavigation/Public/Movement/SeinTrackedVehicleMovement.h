/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.h
 * @brief   Tracked-vehicle locomotion — pivot-vs-arc state machine.
 *
 *          Tracked vehicles have a choice every tick: drive in an arc (like
 *          a wheeled vehicle, turning while moving) or pivot in place
 *          (stop, rotate to face, then drive). The reference for "smart
 *          tracked behavior" is Company of Heroes — tanks lean toward arc
 *          when already aligned and at speed, but commit to a stop-and-pivot
 *          when the aim error is large enough that arcing would carve a
 *          ridiculous path.
 *
 *          The decision is driven by two signals:
 *            - `dot(forward, toTarget)`: how aligned the unit is with its
 *              steering target (1 = perfect, 0 = perpendicular, -1 = behind)
 *            - `|CurrentSpeed|`: how fast the unit is currently moving
 *
 *          And three thresholds:
 *            - `BrakeAlignDot` — below this, a moving unit BRAKES (target
 *              speed = 0). It will stop and then pivot.
 *            - `ArcAlignDot`   — above this, the unit drives at full speed.
 *            - Between the two — linear arc throttle interpolates between 0
 *              and full. This is the "drive while turning" band.
 *            - `PivotEntrySpeed` — speed at or below which a misaligned
 *              unit pivots in place (full TurnRate, no forward motion)
 *              instead of arcing. Above this, it brakes-then-pivots.
 *
 *          Path-following uses the same look-ahead helper as wheeled.
 *          Arrival uses kinematic braking + the shared overshoot fallback.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinLocomotion.h"
#include "Types/FixedPoint.h"
#include "SeinTrackedVehicleMovement.generated.h"

UCLASS(meta = (DisplayName = "Tracked Vehicle"))
class SEINARTSNAVIGATION_API USeinTrackedVehicleMovement : public USeinLocomotion
{
	GENERATED_BODY()

public:

	USeinTrackedVehicleMovement();

	/** Look-ahead distance (world units) along the path for the steering
	 *  carrot point. Larger values produce smoother arcs through corners but
	 *  cut tighter past waypoints. */
	UPROPERTY(EditAnywhere, Category = "Tracked", meta = (ClampMin = "0.0"))
	FFixedPoint LookAheadDistance;

	/** Optional minimum-slowdown distance layered on top of kinematic
	 *  braking (`v_max = √(2·Deceleration·DistanceToFinal)`). Use only for a
	 *  "cautious arrival" feel; 0 = pure physics. */
	UPROPERTY(EditAnywhere, Category = "Tracked", meta = (ClampMin = "0.0"))
	FFixedPoint ArrivalSlowdownDistance;

	/** Alignment dot (forward · toTarget) at or above which the unit drives
	 *  at full speed. Default 0.95 ≈ 18° aim error. Lower the threshold to
	 *  let the unit accelerate through wider misalignments (more "rolling"
	 *  feel); raise it to demand near-perfect alignment before committing
	 *  to full throttle. */
	UPROPERTY(EditAnywhere, Category = "Tracked", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	FFixedPoint ArcAlignDot;

	/** Alignment dot below which a MOVING unit brakes to a stop instead of
	 *  arcing. Default 0.3 ≈ 72° aim error. Below this, arcing would
	 *  produce an absurdly wide path — better to brake, pivot, and then
	 *  drive. Between BrakeAlignDot and ArcAlignDot the unit drives in an
	 *  arc with throttle interpolating linearly. */
	UPROPERTY(EditAnywhere, Category = "Tracked", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	FFixedPoint BrakeAlignDot;

	/** Speed at or below which a misaligned unit pivots in place at full
	 *  TurnRate (no forward motion). Above this speed AND below
	 *  BrakeAlignDot, the unit brakes first, then pivots once stopped.
	 *  Default 50 cm/s — slightly above zero so a unit that's slowed for
	 *  arrival can still pivot if needed. */
	UPROPERTY(EditAnywhere, Category = "Tracked", meta = (ClampMin = "0.0"))
	FFixedPoint PivotEntrySpeed;

	virtual void OnMoveBegin(const FSeinLocomotionContext& Ctx) override;
	virtual bool Tick(const FSeinLocomotionContext& Ctx) override;

protected:

	/** Latched-at-OnMoveBegin reverse decision. Tracked needs no
	 *  steering-inversion (yaw control is direct, no bicycle); reversing
	 *  flips the steering target so the BACK faces the goal, and the state
	 *  machine still drives the pivot/arc choice off `forward·target_dir`. */
	bool bIsReversing = false;
};
