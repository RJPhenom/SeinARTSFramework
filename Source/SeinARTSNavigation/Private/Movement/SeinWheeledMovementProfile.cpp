#include "Movement/SeinWheeledMovementProfile.h"
#include "Lib/SeinSteeringMath.h"
#include "SeinPathTypes.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Types/Rotator.h"
#include "Types/Quat.h"
#include "Components/SeinMovementData.h"
#include "Math/MathLib.h"

bool USeinWheeledMovementProfile::AdvanceAlongPath(
	FSeinEntity& Entity,
	const FSeinMovementData& MoveComp,
	const FSeinPath& Path,
	int32& WaypointIndex,
	const FFixedVector& FinalDestination,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	FSeinMovementKinematicState& KinState,
	int32& OutWaypointReached) const
{
	OutWaypointReached = -1;

	const int32 WaypointCount = Path.GetWaypointCount();
	if (WaypointCount == 0) return true;

	FFixedVector CurrentPos = Entity.Transform.GetLocation();
	FFixedVector CurrentForward = Entity.Transform.GetQuaternionRotation().GetForwardVector();

	while (WaypointIndex < WaypointCount)
	{
		const FFixedVector& WP = Path.Waypoints[WaypointIndex];
		const FFixedPoint DistSq = FFixedVector::DistSquared(CurrentPos, WP);
		if (DistSq <= AcceptanceRadiusSq)
		{
			OutWaypointReached = WaypointIndex;
			WaypointIndex++;
		}
		else break;
	}

	if (WaypointIndex >= WaypointCount)
	{
		Entity.Transform.SetLocation(CurrentPos);
		return true;
	}

	const FFixedVector& TargetWP = Path.Waypoints[WaypointIndex];
	FFixedVector ToWP = TargetWP - CurrentPos;
	FFixedPoint DistToWP = ToWP.Size();
	FFixedVector DesiredForward = (DistToWP > FFixedPoint::Zero) ? (ToWP / DistToWP) : CurrentForward;

	const FFixedPoint MaxSpeed = MoveComp.MoveSpeed;
	const FFixedPoint AccelDelta = MoveComp.Acceleration * DeltaTime;

	// Effective turn rate scales with |speed|/MaxSpeed
	FFixedPoint AbsSpeed = KinState.CurrentSpeed < FFixedPoint::Zero ? -KinState.CurrentSpeed : KinState.CurrentSpeed;
	FFixedPoint SpeedRatio = (MaxSpeed > FFixedPoint::Zero) ? (AbsSpeed / MaxSpeed) : FFixedPoint::Zero;
	SpeedRatio = SeinMath::Clamp(SpeedRatio, FFixedPoint::Zero, FFixedPoint::One);
	FFixedPoint EffectiveTurnRate = MoveComp.TurnRate * SpeedRatio;

	FFixedVector NewForward = SeinSteeringMath::ApplyTurnRateLimit(CurrentForward, DesiredForward, EffectiveTurnRate, DeltaTime);
	{
		FFixedPoint Yaw = SeinMath::Atan2(NewForward.Y, NewForward.X);
		FFixedRotator NewRot(FFixedPoint::Zero, Yaw, FFixedPoint::Zero);
		Entity.Transform.SetRotation(NewRot.Quaternion());
	}

	FFixedPoint Alignment = FFixedVector::DotProduct(NewForward, DesiredForward);

	FFixedPoint TargetSpeed;
	if (Alignment > AlignmentFullThrottle)
	{
		TargetSpeed = MaxSpeed;
	}
	else if (Alignment > AlignmentPartialThrottle)
	{
		TargetSpeed = MaxSpeed * PartialThrottleSpeedFraction;
	}
	else
	{
		// Reverse maneuver: crude 3-point-turn approximation.
		// Note: V1 fallback — may oscillate on pathological S-curves.
		// Future: Dubins/Reeds-Shepp smoothing in BuildPath.
		TargetSpeed = -(MaxSpeed * ReverseSpeedFraction);
	}

	// Accelerate toward TargetSpeed (signed)
	if (KinState.CurrentSpeed < TargetSpeed)
	{
		KinState.CurrentSpeed = KinState.CurrentSpeed + AccelDelta;
		if (KinState.CurrentSpeed > TargetSpeed) KinState.CurrentSpeed = TargetSpeed;
	}
	else if (KinState.CurrentSpeed > TargetSpeed)
	{
		KinState.CurrentSpeed = KinState.CurrentSpeed - AccelDelta;
		if (KinState.CurrentSpeed < TargetSpeed) KinState.CurrentSpeed = TargetSpeed;
	}

	FFixedVector Delta = NewForward * (KinState.CurrentSpeed * DeltaTime);
	CurrentPos = CurrentPos + Delta;
	Entity.Transform.SetLocation(CurrentPos);

	const FFixedPoint DistToDestSq = FFixedVector::DistSquared(CurrentPos, FinalDestination);
	if (DistToDestSq <= AcceptanceRadiusSq)
	{
		return true;
	}
	return false;
}
