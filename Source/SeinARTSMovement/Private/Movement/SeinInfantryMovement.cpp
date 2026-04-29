/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.cpp
 */

#include "Movement/SeinInfantryMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMovement, Log, All);

void USeinInfantryMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// MoveData.CurrentSpeed is intentionally preserved — momentum carries
	// across order changes. With high Acceleration / Deceleration the
	// transition from one velocity to another is near-instant but never a
	// hard zero-snap.
}

bool USeinInfantryMovement::Tick(const FSeinMovementContext& Ctx)
{
	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementData& MoveData = Ctx.MoveData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	// Pull persisted speed off the component — preserves momentum across
	// reorders. Written back at end of tick.
	FFixedPoint CurrentSpeed = MoveData.CurrentSpeed;

	const FFixedVector PrePos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// Arrival — within AcceptanceRadius OR overshoot (close + slow + heading
	// away). No position snap; stopping inside the acceptance envelope is
	// the contract.
	{
		FFixedVector ToFinal = FinalWp - PrePos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;

		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MoveData.MoveSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = IsOvershootArrival(
			PrePos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);

		// Cluster arrival — when a friendly unit has parked nearby AND we're
		// in the vicinity of the destination, accept this position as
		// "arrived." Lets group moves settle into a tight formation around
		// the goal without explicit slot assignment.
		const bool bClusterArrival = IsClusterArrival(Ctx, FinalWp);

		if (bWithinAcceptance || bOvershoot || bClusterArrival)
		{
			MoveData.CurrentSpeed = FFixedPoint::Zero;
			return true;
		}
	}

	// Advance through waypoints we've effectively passed. Tighter radius
	// than vehicles — infantry navigates more precisely.
	const FFixedPoint OneStep = MoveData.MoveSpeed * DeltaTime;
	const FFixedPoint AdvanceRadiusSq = OneStep * OneStep;
	while (CurrentWaypointIndex < N - 1)
	{
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - PrePos;
		ToWp.Z = FFixedPoint::Zero;
		if (ToWp.SizeSquared() <= AdvanceRadiusSq) ++CurrentWaypointIndex;
		else break;
	}

	// Steer toward the current waypoint directly — infantry doesn't need
	// look-ahead carrots; pivot is fast enough that immediate-waypoint
	// targeting produces clean motion.
	const FFixedVector TargetWp = Path.Waypoints[CurrentWaypointIndex];
	FFixedVector ToTarget = TargetWp - PrePos;
	ToTarget.Z = FFixedPoint::Zero;
	const FFixedPoint DistToTargetSq = ToTarget.SizeSquared();

	// Compute the unit vector toward the target. If the target is on top
	// of the unit (zero-length), hold previous facing and brake to zero.
	// Avoidance is blended into the desired-velocity vector — perpendicular
	// repulsion shows up as a steered direction; rotation chases the new
	// velocity at TurnRate (existing logic at the bottom of this function),
	// so the unit visibly arcs around neighbors.
	FFixedVector Dir = FFixedVector::ZeroVector;
	if (DistToTargetSq > FFixedPoint::Epsilon)
	{
		Dir = FFixedVector::GetSafeNormal(ToTarget);
	}

	if (MoveData.FootprintRadius > FFixedPoint::Zero && MoveData.AvoidanceWeight > FFixedPoint::Zero)
	{
		const FFixedVector DesiredVel(
			Dir.X * MoveData.MoveSpeed,
			Dir.Y * MoveData.MoveSpeed,
			FFixedPoint::Zero);
		const FFixedVector AvoidBias = ComputeAvoidanceVector(Ctx, DesiredVel);
		if (AvoidBias.SizeSquared() > FFixedPoint::Epsilon)
		{
			FFixedVector Steered = DesiredVel + AvoidBias;
			Steered.Z = FFixedPoint::Zero;
			if (Steered.SizeSquared() > FFixedPoint::Epsilon)
			{
				Dir = FFixedVector::GetSafeNormal(Steered);
			}
		}
	}

	// Smoothstep speed toward MoveSpeed, capped by kinematic-arrival braking
	// against the FINAL waypoint so the unit visibly slows on approach
	// instead of running flat-out into a hard stop. With high Accel/Decel
	// (recommended for snappy infantry) the brake zone is small — usually
	// less than a step — and visually feels nearly instant, but the curve
	// remains continuous.
	FFixedPoint TargetSpeed = (DistToTargetSq > FFixedPoint::Epsilon)
		? MoveData.MoveSpeed : FFixedPoint::Zero;
	{
		FFixedVector ToFinal = FinalWp - PrePos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();
		const FFixedPoint MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, MoveData.Deceleration);
		if (MaxArrivalSpeed < TargetSpeed) TargetSpeed = MaxArrivalSpeed;
	}
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MoveData.Acceleration, MoveData.Deceleration, DeltaTime);

	// Translate. Don't overshoot the target waypoint within one tick — clamp
	// step to the planar distance so we land exactly there if we'd pass it.
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = PrePos;
	if (Dir.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint Dist = SeinMath::Sqrt(DistToTargetSq);
		const FFixedPoint ClampedStep = (StepLen < Dist) ? StepLen : Dist;
		NewPos.X = NewPos.X + Dir.X * ClampedStep;
		NewPos.Y = NewPos.Y + Dir.Y * ClampedStep;
	}

	// Hard nav-collision resolve before Z-snap so axis-slide chooses the right
	// XY first, then Z reflects the actual cell we end up at.
	NewPos = ResolveNavCollision(PrePos, NewPos, Nav);

	// Z-snap to nav ground.
	if (Nav)
	{
		FFixedPoint GroundZ;
		if (Nav->GetGroundHeightAt(NewPos, GroundZ))
		{
			NewPos.Z = GroundZ;
		}
	}

	Entity.Transform.SetLocation(NewPos);

	// Smooth turn-to-velocity from the actual moved delta. Velocity-based
	// steering (rather than waypoint-direction-based) means a blocked tick
	// naturally holds facing — no slewing back to identity.
	FFixedVector Delta = NewPos - PrePos;
	Delta.Z = FFixedPoint::Zero;
	if (Delta.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(Delta.Y, Delta.X);
		const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MoveData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		Entity.Transform.Rotation = YawOnly(CurrentYaw + AppliedTurn);

		UE_LOG(LogSeinMovement, Verbose,
			TEXT("Infantry: pre=(%.2f,%.2f) post=(%.2f,%.2f) speed=%.2f→%.2f delta=(%.3f,%.3f) wp[%d/%d] yaw=%.3f→%.3f"),
			PrePos.X.ToFloat(), PrePos.Y.ToFloat(),
			NewPos.X.ToFloat(), NewPos.Y.ToFloat(),
			MoveData.CurrentSpeed.ToFloat(), CurrentSpeed.ToFloat(),
			Delta.X.ToFloat(), Delta.Y.ToFloat(),
			CurrentWaypointIndex, N,
			CurrentYaw.ToFloat(), (CurrentYaw + AppliedTurn).ToFloat());
	}

	// Persist for next tick / next move action.
	MoveData.CurrentSpeed = CurrentSpeed;
	return false;
}
