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

bool USeinBasicMovement::Tick(const FSeinMovementContext& Ctx)
{
	FSeinEntity& Entity = Ctx.Entity;
	const FSeinMovementData& MoveData = Ctx.MoveData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	// Cluster arrival short-circuit. If we're in the vicinity of the final
	// waypoint and a friendly unit has parked nearby, declare arrival.
	// Without this, basic-movement cluster moves stack at the destination
	// while penetration resolution shoves them — visible jitter.
	if (Path.Waypoints.Num() > 0)
	{
		const FFixedVector& FinalWp = Path.Waypoints.Last();
		if (IsClusterArrival(Ctx, FinalWp))
		{
			CurrentWaypointIndex = Path.Waypoints.Num();
			return true;
		}
	}

	// Advance along path: seek toward current waypoint at MoveSpeed, pop
	// waypoints we reach within one step. Planar XY motion only; Z is sampled
	// from the nav at the end so the unit stays flush with local terrain
	// instead of popping to an upcoming waypoint's Z.
	const FFixedVector InitialPos = Entity.Transform.GetLocation();
	FFixedVector Pos = InitialPos;
	FFixedPoint RemainingStep = MoveData.MoveSpeed * DeltaTime;

	// Compute one-shot avoidance bias for this tick. Basic blends it into the
	// step direction — simplest form of avoidance, no smoothing or steering
	// authority logic. Designers using Basic for cursors / debug units who
	// don't want avoidance just leave FootprintRadius at 0.
	const FFixedVector AvoidBias = ComputeAvoidanceVector(Ctx, FFixedVector::ZeroVector);

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
		// Avoidance bias is added to the path direction before normalizing —
		// the result is still a unit vector but tilted away from neighbors.
		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint StepLen = (Dist < RemainingStep) ? Dist : RemainingStep;

		FFixedVector Dir = FFixedVector::GetSafeNormal(Delta);
		if (AvoidBias.SizeSquared() > FFixedPoint::Epsilon)
		{
			// Re-normalize after bias add so step length stays = StepLen.
			FFixedVector Biased = Dir + AvoidBias / MoveData.MoveSpeed; // bias is in MoveSpeed units; rescale to unit
			Biased.Z = FFixedPoint::Zero;
			if (Biased.SizeSquared() > FFixedPoint::Epsilon)
			{
				Dir = FFixedVector::GetSafeNormal(Biased);
			}
		}

		Pos.X = Pos.X + Dir.X * StepLen;
		Pos.Y = Pos.Y + Dir.Y * StepLen;

		RemainingStep = RemainingStep - StepLen;
	}

	// Hard nav-collision resolve. Wall avoidance is a steering hint; this is
	// the floor that prevents the unit from clipping through static blockers
	// when path-attraction + bias + momentum overpower the soft repulsion.
	Pos = ResolveNavCollision(InitialPos, Pos, Nav);

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
