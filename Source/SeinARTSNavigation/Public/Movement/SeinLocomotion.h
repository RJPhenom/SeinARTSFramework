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
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinLocomotion.generated.h"

class USeinNavigation;
class USeinWorldSubsystem;
struct FSeinEntity;
struct FSeinMovementData;
struct FSeinPath;

/**
 * Per-tick context bundle handed to a locomotion. One struct so we can add new
 * services (avoidance, formation, threat field) without further signature
 * churn — locomotion subclasses just read what they care about and ignore
 * the rest. All references are valid for the duration of a single Tick call.
 */
struct FSeinLocomotionContext
{
	FSeinEntity& Entity;
	FSeinMovementData& MoveData;
	const FSeinPath& Path;
	int32& CurrentWaypointIndex;
	FFixedPoint AcceptanceRadiusSq;
	FFixedPoint DeltaTime;
	USeinNavigation* Nav;
	USeinWorldSubsystem* World;     // gives access to spatial hash + components
	FSeinEntityHandle SelfHandle;   // self-exclusion in spatial queries
};

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Sein Locomotion"))
class SEINARTSNAVIGATION_API USeinLocomotion : public UObject
{
	GENERATED_BODY()

public:

	/** Called once when a MoveTo action resolves a path (before the first
	 *  Tick). Default: no-op. Override to initialize subclass state.
	 *  Receives the same context shape as Tick — convenient for one-shot
	 *  decisions (e.g. auto-reverse latch) that depend on neighbor queries
	 *  or world services. MoveData is non-const so subclasses can read
	 *  carry-over runtime state (e.g. CurrentSpeed) — preserve it instead
	 *  of zeroing if you want momentum to flow across order changes. */
	virtual void OnMoveBegin(const FSeinLocomotionContext& Ctx) {}

	/** Called each sim tick while the move is active. Mutates Entity.Transform
	 *  and writes runtime state back through MoveData (e.g. CurrentSpeed).
	 *  Advances CurrentWaypointIndex as waypoints are consumed.
	 *  @return true when the entity has reached the final waypoint. */
	virtual bool Tick(const FSeinLocomotionContext& Ctx)
		PURE_VIRTUAL(USeinLocomotion::Tick, return true;);

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

	/** Walk forward along the path polyline starting at AgentPos and return
	 *  the point reached after consuming `LookAhead` world-units of travel.
	 *  The first segment is (AgentPos → Path.Waypoints[CurrentWaypointIndex])
	 *  so the carrot is always strictly ahead of the unit; subsequent
	 *  segments connect successive waypoints. If `LookAhead` exceeds the
	 *  remaining path length, returns the final waypoint (clamped). Returns
	 *  AgentPos when the path is empty or the index is past the end.
	 *  Z is preserved from the source points without interpolation —
	 *  ground-snap is the locomotion's job. */
	static FFixedVector ResolveLookAheadPoint(
		const FFixedVector& AgentPos,
		const FSeinPath& Path,
		int32 CurrentWaypointIndex,
		FFixedPoint LookAhead);

	/** Smooth-step a scalar speed toward `Target`. Picks `Accel` rate when the
	 *  speed magnitude is growing (or sign-flipping through zero), `Decel`
	 *  when shrinking — matches the asymmetric throttle/brake feel of
	 *  vehicles where braking is harder than accel. Returns the new speed
	 *  after one tick of `Dt`. Pure scalar — sign carries reverse direction. */
	static FFixedPoint StepSpeedToward(
		FFixedPoint Current, FFixedPoint Target,
		FFixedPoint Accel, FFixedPoint Decel, FFixedPoint Dt);

	/** Maximum speed at which the unit can still brake to zero EXACTLY at
	 *  `DistToFinal` ahead, given `Deceleration`. Solves v² = 2·a·d for v.
	 *  Returns a very large value when Deceleration <= 0 (no kinematic cap
	 *  desired; falls back to MoveSpeed). Used by all vehicle locomotions to
	 *  produce physics-correct arrival ramps without designer tuning. */
	static FFixedPoint KinematicArrivalSpeedCap(
		FFixedPoint DistToFinal, FFixedPoint Deceleration);

	/** Hard nav-collision resolve for a translation step. Wall avoidance is a
	 *  steering FORCE — it can be overcome by path-attraction + momentum, so
	 *  units can clip through static blocked cells. This is the floor: if
	 *  `NewPos` lands on an impassable cell, try axis-only slides
	 *  (X-only, then Y-only) so the unit skims grid-aligned walls instead of
	 *  stopping dead; if both axes are blocked, hold at `OldPos`.
	 *
	 *  Static-only by design — `Nav->IsPassable` reads the bake, not the
	 *  dynamic blocker overlay (that lives on a per-FindPath path); blocking
	 *  vehicles against each other is penetration resolution's job. So this
	 *  is "don't walk through walls", not "don't walk through tanks." */
	static FFixedVector ResolveNavCollision(
		const FFixedVector& OldPos,
		const FFixedVector& NewPos,
		USeinNavigation* Nav);

	/** Graceful-stop trigger — returns true when the unit has effectively
	 *  overshot the destination and can't physically circle back tightly
	 *  enough to land inside AcceptanceRadius. Three gates that must ALL
	 *  hold:
	 *      1. Within `VicinityRadiusSq` of `FinalWp` (close to goal)
	 *      2. |CurrentSpeed| <= `MaxSpeedForOvershoot` (winding down)
	 *      3. forward · toFinal < 0 (heading away from goal)
	 *  The vicinity + speed gates together prevent false fires during
	 *  legitimate U-turn maneuvers, where the unit is still far from the
	 *  goal AND at high speed during the arc. */
	static bool IsOvershootArrival(
		const FFixedVector& AgentPos,
		const FFixedVector& FinalWp,
		const FFixedQuaternion& Rotation,
		FFixedPoint CurrentSpeed,
		FFixedPoint VicinityRadiusSq,
		FFixedPoint MaxSpeedForOvershoot);

	/** Auto-reverse decision — returns true if the unit should drive in
	 *  reverse to reach `FinalGoal` instead of forward. Triggers when ALL of:
	 *      1. `MoveData.bCanReverse` is true
	 *      2. distance(AgentPos, FinalGoal) <= MoveData.ReverseEngageDistance
	 *      3. forward · normalize(toGoal) <= MoveData.ReverseEngageDotThreshold
	 *  Far-away rear targets fail the distance gate and U-turn forward.
	 *  Caller decides when to query — typically once at OnMoveBegin and
	 *  again on each repath; oscillation hysteresis is not built into this
	 *  helper, so per-tick polling without state needs care. */
	static bool ShouldAutoReverse(
		const FFixedVector& AgentPos,
		const FFixedQuaternion& Rotation,
		const FFixedVector& FinalGoal,
		const FSeinMovementData& MoveData);

	/** Cluster / group arrival heuristic. Returns true when ALL of:
	 *      1. The unit is within (3 × √AcceptanceRadiusSq) of `FinalGoal`
	 *         (planar XY distance — "close enough to call this the
	 *         destination, regardless of who got the exact spot").
	 *      2. There is at least one stationary neighbor (CurrentSpeed ≈ 0)
	 *         within (FootprintRadius × 2) of the unit (someone arrived
	 *         first and is parked between us and the goal).
	 *  Lets cluster moves settle into a tight formation around the
	 *  destination without explicit slot-assignment logic — first arriver
	 *  parks at the literal goal, subsequent arrivers each call it done
	 *  when they bump into a parked friend. Penetration resolution then
	 *  enforces non-overlap.
	 *  Caller is each locomotion's arrival branch (OR with bWithinAcceptance
	 *  / bOvershoot). Returns false when avoidance is opted out
	 *  (FootprintRadius == 0) since the heuristic relies on neighbor data. */
	static bool IsClusterArrival(
		const FSeinLocomotionContext& Ctx,
		const FFixedVector& FinalGoal);

	/** Anticipation-style avoidance: query the world's spatial hash for
	 *  neighbors near `Ctx.Entity`, project closest-approach time against
	 *  each, and accumulate a world-space velocity-bias vector that nudges
	 *  the unit perpendicular to the imminent collision course. Magnitude
	 *  scales with both `AvoidanceWeight` (designer knob) and how soon the
	 *  collision would occur. Pure scalar math — deterministic.
	 *
	 *  Caller decides how to consume the bias:
	 *    - Basic blends it into the position step direction
	 *    - Infantry adds it to the desired-velocity vector
	 *    - Wheeled converts it to a yaw delta (steering authority limited)
	 *    - Tracked uses it to shift the steering carrot
	 *
	 *  Returns FFixedVector::ZeroVector when avoidance is off (zero
	 *  footprint, zero weight) or there are no relevant neighbors. */
	static FFixedVector ComputeAvoidanceVector(
		const FSeinLocomotionContext& Ctx,
		const FFixedVector& DesiredVelocity,
		FFixedPoint LookAheadSeconds = FFixedPoint::Half);
};
