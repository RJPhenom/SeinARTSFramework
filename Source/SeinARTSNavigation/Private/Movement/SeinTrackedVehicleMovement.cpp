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

bool USeinTrackedVehicleMovement::Tick(
	FSeinEntity& Entity,
	const FSeinMovementData& MoveData,
	const FSeinPath& Path,
	int32& CurrentWaypointIndex,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	USeinNavigation* Nav)
{
	if (CurrentWaypointIndex >= Path.Waypoints.Num()) return true;

	FFixedVector Pos = Entity.Transform.GetLocation();
	const FFixedVector Target = Path.Waypoints[CurrentWaypointIndex];

	FFixedVector ToTarget = Target - Pos;
	ToTarget.Z = FFixedPoint::Zero;
	const FFixedPoint DistSq = ToTarget.SizeSquared();

	const bool bIsFinalWaypoint = (CurrentWaypointIndex == Path.Waypoints.Num() - 1);
	const FFixedPoint OneStep = MoveData.MoveSpeed * DeltaTime;
	const FFixedPoint ArriveRadiusSq = bIsFinalWaypoint ? AcceptanceRadiusSq : OneStep * OneStep;
	if (DistSq <= ArriveRadiusSq)
	{
		Pos.X = Target.X;
		Pos.Y = Target.Y;
		++CurrentWaypointIndex;
		Entity.Transform.SetLocation(Pos);
		return CurrentWaypointIndex >= Path.Waypoints.Num();
	}

	// Aim calculation.
	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
	const FFixedPoint DesiredYaw = SeinMath::Atan2(ToTarget.Y, ToTarget.X);
	const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
	const FFixedPoint MaxTurn = MoveData.TurnRate * DeltaTime;
	const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
	const FFixedPoint NewYaw = CurrentYaw + AppliedTurn;
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// Pivot-vs-drive decision: if the aim error is still above the threshold
	// AFTER this tick's turn, stay stationary and rotate in place. Once the
	// applied turn brings the remaining error under the threshold, allow
	// translation this same tick so units don't lose a frame at the boundary.
	const FFixedPoint PivotThreshold = FFixedPoint::FromFloat(PivotThresholdRadians);
	const FFixedPoint RemainingErr = SeinMath::Abs(YawDelta - AppliedTurn);
	const bool bAlignedEnoughToMove = (RemainingErr <= PivotThreshold);

	if (bAlignedEnoughToMove)
	{
		const FFixedPoint CosY = SeinMath::Cos(NewYaw);
		const FFixedPoint SinY = SeinMath::Sin(NewYaw);
		Pos.X = Pos.X + CosY * OneStep;
		Pos.Y = Pos.Y + SinY * OneStep;

		if (Nav)
		{
			FFixedPoint GroundZ;
			if (Nav->GetGroundHeightAt(Pos, GroundZ))
			{
				Pos.Z = GroundZ;
			}
		}
	}

	Entity.Transform.SetLocation(Pos);
	return false;
}
