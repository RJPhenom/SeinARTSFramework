/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.cpp
 */

#include "Movement/SeinTrackedVehicleMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinTracked, Log, All);

USeinTrackedVehicleMovement::USeinTrackedVehicleMovement()
	: LookAheadDistance(FFixedPoint::FromInt(200))
	, ArrivalSlowdownDistance(FFixedPoint::Zero) // 0 = pure kinematic braking
	// Defaults expressed as integer ratios so the CDO ctor never runs a
	// runtime float→fixed conversion. Bit-identical across architectures.
	, ArcAlignDot(FFixedPoint::FromInt(95) / FFixedPoint::FromInt(100))   // 0.95 ≈ 18° error
	, BrakeAlignDot(FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10))   // 0.3 ≈ 72° error
	, PivotEntrySpeed(FFixedPoint::FromInt(50))
{
}

void USeinTrackedVehicleMovement::OnMoveBegin(const FSeinLocomotionContext& Ctx)
{
	FSeinEntity& Entity = Ctx.Entity;
	const FSeinMovementData& MoveData = Ctx.MoveData;
	const FSeinPath& Path = Ctx.Path;

	// Preserve MoveData.CurrentSpeed so reorders carry momentum.
	const int32 N = Path.Waypoints.Num();
	bIsReversing = (N > 0) && ShouldAutoReverse(
		Entity.Transform.GetLocation(),
		Entity.Transform.Rotation,
		Path.Waypoints[N - 1],
		MoveData);
}

bool USeinTrackedVehicleMovement::Tick(const FSeinLocomotionContext& Ctx)
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

	FFixedPoint CurrentSpeed = MoveData.CurrentSpeed;

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// Arrival — within AcceptanceRadius OR overshoot. No position snap.
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;

		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MoveData.MoveSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = IsOvershootArrival(
			AgentPos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);

		const bool bClusterArrival = IsClusterArrival(Ctx, FinalWp);

		if (bWithinAcceptance || bOvershoot || bClusterArrival)
		{
			UE_LOG(LogSeinTracked, Verbose,
				TEXT("Tracked arrival: within=%d overshoot=%d cluster=%d distSq=%.1f speed=%.2f"),
				bWithinAcceptance ? 1 : 0, bOvershoot ? 1 : 0, bClusterArrival ? 1 : 0,
				ToFinal.SizeSquared().ToFloat(), CurrentSpeed.ToFloat());
			MoveData.CurrentSpeed = FFixedPoint::Zero;
			return true;
		}
	}

	// Advance through waypoints we've effectively passed.
	const FFixedPoint OneStep = MoveData.MoveSpeed * DeltaTime;
	const FFixedPoint AdvanceRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
		? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
	const FFixedPoint AdvanceRadiusSq = AdvanceRadius * AdvanceRadius;
	while (CurrentWaypointIndex < N - 1)
	{
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - AgentPos;
		ToWp.Z = FFixedPoint::Zero;
		if (ToWp.SizeSquared() <= AdvanceRadiusSq) ++CurrentWaypointIndex;
		else break;
	}

	// Steering target on the polyline.
	const FFixedPoint LookAhead = (LookAheadDistance > FFixedPoint::Zero)
		? LookAheadDistance : FFixedPoint::FromInt(100);
	const FFixedVector LookAheadPoint = ResolveLookAheadPoint(AgentPos, Path, CurrentWaypointIndex, LookAhead);

	FFixedVector ToTarget = LookAheadPoint - AgentPos;
	ToTarget.Z = FFixedPoint::Zero;

	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);

	// Avoidance: shift the steering carrot away from neighbors. Tracked
	// steering authority is high (can pivot in place), so the bias maps
	// directly onto the carrot offset rather than a steering-angle nudge —
	// the existing pivot/arc state machine then chooses the best response
	// for the new carrot. Magnitude is in MoveSpeed units; we scale to the
	// look-ahead radius so the effective carrot stays a reasonable distance
	// from the unit even when many neighbors push hard.
	if (MoveData.FootprintRadius > FFixedPoint::Zero && MoveData.AvoidanceWeight > FFixedPoint::Zero)
	{
		// Use current forward velocity as the desired-velocity hint.
		const FFixedPoint CurFwdX = SeinMath::Cos(CurrentYaw);
		const FFixedPoint CurFwdY = SeinMath::Sin(CurrentYaw);
		const FFixedVector DesiredVel(
			CurFwdX * MoveData.MoveSpeed,
			CurFwdY * MoveData.MoveSpeed,
			FFixedPoint::Zero);
		const FFixedVector AvoidBias = ComputeAvoidanceVector(Ctx, DesiredVel);
		if (AvoidBias.SizeSquared() > FFixedPoint::Epsilon && MoveData.MoveSpeed > FFixedPoint::Epsilon)
		{
			// Scale bias from MoveSpeed-units to look-ahead-distance-units.
			const FFixedPoint ShiftScale = LookAhead / MoveData.MoveSpeed;
			ToTarget.X = ToTarget.X + AvoidBias.X * ShiftScale;
			ToTarget.Y = ToTarget.Y + AvoidBias.Y * ShiftScale;
		}
	}

	// Reversing flips the steering target — we want the BACK of the chassis
	// pointed at the goal, so the chassis "forward" should aim away from it.
	// The rest of the state machine is unchanged: alignment dot is still
	// `forward · steering_target_dir`, just measured against the flipped
	// direction. Pivot/arc/brake choices stay valid because they're about
	// chassis-vs-target alignment, regardless of which end of the chassis
	// is the leading edge.
	FFixedVector EffectiveToTarget = bIsReversing ? -ToTarget : ToTarget;

	// Apply this tick's rotation step (clamped by TurnRate). For tracked
	// vehicles, the same TurnRate cap covers BOTH pivot-in-place and
	// arc-while-driving — they don't have differentiated turn rates the way
	// a wheeled bicycle model does.
	FFixedPoint NewYaw = CurrentYaw;
	FFixedPoint AlignDotPostTurn = FFixedPoint::One; // assume aligned if no target
	if (EffectiveToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(EffectiveToTarget.Y, EffectiveToTarget.X);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MoveData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		NewYaw = CurrentYaw + AppliedTurn;

		// Recompute alignment from POST-turn forward against the same target.
		const FFixedVector ToTargetN = FFixedVector::GetSafeNormal(EffectiveToTarget);
		const FFixedPoint NewFwdX = SeinMath::Cos(NewYaw);
		const FFixedPoint NewFwdY = SeinMath::Sin(NewYaw);
		AlignDotPostTurn = NewFwdX * ToTargetN.X + NewFwdY * ToTargetN.Y;
	}
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// State machine — the throttle multiplier "ThrottleByAlign":
	//   moving + dot < BrakeAlignDot      → brake (0)
	//   slow   + dot < ArcAlignDot        → pivot (0, rotation already applied)
	//   dot >= ArcAlignDot                → full speed (1)
	//   else (arc band)                   → linear interpolation
	const FFixedPoint AbsSpeed = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;

	FFixedPoint ThrottleByAlign;
	if (AbsSpeed >= PivotEntrySpeed && AlignDotPostTurn < BrakeAlignDot)
	{
		// Moving fast and very misaligned — brake, then pivot once stopped.
		ThrottleByAlign = FFixedPoint::Zero;
	}
	else if (AbsSpeed < PivotEntrySpeed && AlignDotPostTurn < ArcAlignDot)
	{
		// At/near rest, misaligned — pivot in place at full TurnRate.
		ThrottleByAlign = FFixedPoint::Zero;
	}
	else if (AlignDotPostTurn >= ArcAlignDot)
	{
		ThrottleByAlign = FFixedPoint::One;
	}
	else
	{
		// Arc band — linear interpolation between BrakeAlignDot (0 throttle)
		// and ArcAlignDot (full throttle).
		const FFixedPoint Span = ArcAlignDot - BrakeAlignDot;
		if (Span <= FFixedPoint::Epsilon)
		{
			ThrottleByAlign = FFixedPoint::One;
		}
		else
		{
			ThrottleByAlign = (AlignDotPostTurn - BrakeAlignDot) / Span;
			if (ThrottleByAlign < FFixedPoint::Zero) ThrottleByAlign = FFixedPoint::Zero;
		}
	}

	// Kinematic arrival cap (and optional designer floor). See helper docs
	// on USeinLocomotion::KinematicArrivalSpeedCap.
	FFixedPoint MaxArrivalSpeed;
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();

		MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, MoveData.Deceleration);

		if (ArrivalSlowdownDistance > FFixedPoint::Zero && DistFinal < ArrivalSlowdownDistance)
		{
			const FFixedPoint LinearCap = MoveData.MoveSpeed * (DistFinal / ArrivalSlowdownDistance);
			if (LinearCap < MaxArrivalSpeed) MaxArrivalSpeed = LinearCap;
		}
	}

	// Resolve target magnitude — forward uses MoveSpeed, reverse uses
	// ReverseMaxSpeed (or MoveSpeed/2 fallback). Apply throttle + arrival cap
	// to the magnitude, then restore the sign for reverse.
	FFixedPoint MoveCap;
	if (bIsReversing)
	{
		MoveCap = (MoveData.ReverseMaxSpeed > FFixedPoint::Zero)
			? MoveData.ReverseMaxSpeed
			: MoveData.MoveSpeed * FFixedPoint::Half;
	}
	else
	{
		MoveCap = MoveData.MoveSpeed;
	}
	FFixedPoint TargetSpeedMag = MoveCap * ThrottleByAlign;
	if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
	const FFixedPoint TargetSpeed = bIsReversing ? -TargetSpeedMag : TargetSpeedMag;

	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MoveData.Acceleration, MoveData.Deceleration, DeltaTime);

	// Translate along the post-turn forward vector.
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// Hard nav-collision resolve. Tracked vehicles can pivot in place but
	// still translate forward at TurnRate's mercy; without this, an
	// off-axis steer command + momentum can carry the chassis through
	// blocked cells before the steering catches up.
	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav);

	if (Nav)
	{
		FFixedPoint GroundZ;
		if (Nav->GetGroundHeightAt(NewPos, GroundZ))
		{
			NewPos.Z = GroundZ;
		}
	}

	UE_LOG(LogSeinTracked, Verbose,
		TEXT("Tracked: pos=(%.1f,%.1f) yaw=%.3f→%.3f dot=%.3f speed=%.2f→%.2f throttle=%.2f arrCap=%.1f rev=%d"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(), NewYaw.ToFloat(),
		AlignDotPostTurn.ToFloat(),
		MoveData.CurrentSpeed.ToFloat(), CurrentSpeed.ToFloat(),
		ThrottleByAlign.ToFloat(), MaxArrivalSpeed.ToFloat(),
		bIsReversing ? 1 : 0);

	Entity.Transform.SetLocation(NewPos);
	MoveData.CurrentSpeed = CurrentSpeed;
	return false;
}
