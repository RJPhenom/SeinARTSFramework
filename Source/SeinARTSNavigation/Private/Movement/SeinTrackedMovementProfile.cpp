#include "Movement/SeinTrackedMovementProfile.h"
#include "Lib/SeinSteeringMath.h"
#include "SeinPathTypes.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Types/Rotator.h"
#include "Types/Quat.h"
#include "Components/SeinComponents.h"
#include "Math/MathLib.h"

bool USeinTrackedMovementProfile::AdvanceAlongPath(
	FSeinEntity& Entity,
	const FSeinMovementComponent& MoveComp,
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
	if (WaypointCount == 0)
	{
		return true;
	}

	FFixedVector CurrentPos = Entity.Transform.GetLocation();
	FFixedVector CurrentForward = Entity.Transform.GetQuaternionRotation().GetForwardVector();

	// Skip already-arrived waypoints
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

	// Full turn rate, speed-independent
	FFixedVector NewForward = SeinSteeringMath::ApplyTurnRateLimit(CurrentForward, DesiredForward, MoveComp.TurnRate, DeltaTime);

	// Write orientation
	{
		FFixedPoint Yaw = SeinMath::Atan2(NewForward.Y, NewForward.X);
		FFixedRotator NewRot(FFixedPoint::Zero, Yaw, FFixedPoint::Zero);
		Entity.Transform.SetRotation(NewRot.Quaternion());
	}

	FFixedPoint Alignment = FFixedVector::DotProduct(NewForward, DesiredForward);

	const FFixedPoint MaxSpeed = MoveComp.MoveSpeed;
	const FFixedPoint AccelDelta = MoveComp.Acceleration * DeltaTime;

	if (Alignment > AlignmentForwardThreshold)
	{
		// Accelerate toward MoveSpeed
		if (KinState.CurrentSpeed < MaxSpeed)
		{
			KinState.CurrentSpeed = KinState.CurrentSpeed + AccelDelta;
			if (KinState.CurrentSpeed > MaxSpeed) KinState.CurrentSpeed = MaxSpeed;
		}
	}
	else
	{
		// Brake to zero before pivot
		if (KinState.CurrentSpeed > FFixedPoint::Zero)
		{
			KinState.CurrentSpeed = KinState.CurrentSpeed - AccelDelta;
			if (KinState.CurrentSpeed < FFixedPoint::Zero) KinState.CurrentSpeed = FFixedPoint::Zero;
		}
	}

	// Translate
	FFixedVector Delta = NewForward * (KinState.CurrentSpeed * DeltaTime);
	CurrentPos = CurrentPos + Delta;
	Entity.Transform.SetLocation(CurrentPos);

	// Arrival
	const FFixedPoint DistToDestSq = FFixedVector::DistSquared(CurrentPos, FinalDestination);
	if (DistToDestSq <= AcceptanceRadiusSq)
	{
		return true;
	}
	return false;
}
