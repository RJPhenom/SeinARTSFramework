/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.cpp
 */

#include "Movement/SeinBasicMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"

bool USeinBasicMovement::Tick(
	FSeinEntity& Entity,
	const FSeinMovementData& MoveData,
	const FSeinPath& Path,
	int32& CurrentWaypointIndex,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	USeinNavigation* Nav)
{
	// Advance along path: seek toward current waypoint at MoveSpeed, pop
	// waypoints we reach within one step. Planar XY motion only; Z is sampled
	// from the nav at the end so the unit stays flush with local terrain
	// instead of popping to an upcoming waypoint's Z.
	FFixedVector Pos = Entity.Transform.GetLocation();
	FFixedPoint RemainingStep = MoveData.MoveSpeed * DeltaTime;

	while (RemainingStep > FFixedPoint::Zero && CurrentWaypointIndex < Path.Waypoints.Num())
	{
		const FFixedVector Target = Path.Waypoints[CurrentWaypointIndex];
		FFixedVector Delta = Target - Pos;
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint DistSq = Delta.SizeSquared();

		const bool bIsFinalWaypoint = (CurrentWaypointIndex == Path.Waypoints.Num() - 1);
		const FFixedPoint OneStep = MoveData.MoveSpeed * DeltaTime;
		const FFixedPoint ArriveRadiusSq = bIsFinalWaypoint ? AcceptanceRadiusSq : OneStep * OneStep;

		if (DistSq <= ArriveRadiusSq)
		{
			// Arrived at this waypoint (XY only; Z resolved below).
			Pos.X = Target.X;
			Pos.Y = Target.Y;
			++CurrentWaypointIndex;
			continue;
		}

		// Step toward target, bounded by remaining movement budget this tick.
		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint StepLen = (Dist < RemainingStep) ? Dist : RemainingStep;
		const FFixedVector Dir = FFixedVector::GetSafeNormal(Delta);
		Pos.X = Pos.X + Dir.X * StepLen;
		Pos.Y = Pos.Y + Dir.Y * StepLen;

		RemainingStep = RemainingStep - StepLen;
	}

	// Snap Z to nav ground at agent's current XY.
	if (Nav)
	{
		FFixedPoint GroundZ;
		if (Nav->GetGroundHeightAt(Pos, GroundZ))
		{
			Pos.Z = GroundZ;
		}
	}

	Entity.Transform.SetLocation(Pos);

	return CurrentWaypointIndex >= Path.Waypoints.Num();
}
