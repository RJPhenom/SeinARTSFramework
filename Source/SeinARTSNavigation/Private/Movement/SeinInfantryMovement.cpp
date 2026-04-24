/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.cpp
 */

#include "Movement/SeinInfantryMovement.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Components/SeinMovementData.h"

bool USeinInfantryMovement::Tick(
	FSeinEntity& Entity,
	const FSeinMovementData& MoveData,
	const FSeinPath& Path,
	int32& CurrentWaypointIndex,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	USeinNavigation* Nav)
{
	// Snapshot position before the base advance so we can compute this tick's
	// velocity vector (used to orient facing). Using post-move position minus
	// pre-move avoids re-deriving "direction to current waypoint" which would
	// flicker when an arrival is processed mid-tick.
	const FFixedVector PrePos = Entity.Transform.GetLocation();

	const bool bReached = Super::Tick(Entity, MoveData, Path, CurrentWaypointIndex,
		AcceptanceRadiusSq, DeltaTime, Nav);

	// Face the direction we moved this tick. Skip when movement is negligible
	// (already at destination, blocked, etc.) so we don't clobber the last
	// valid facing with a zero-length delta.
	const FFixedVector Delta = Entity.Transform.GetLocation() - PrePos;
	FFixedVector Planar = Delta;
	Planar.Z = FFixedPoint::Zero;
	if (Planar.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint Yaw = SeinMath::Atan2(Planar.Y, Planar.X);
		Entity.Transform.Rotation = YawOnly(Yaw);
	}

	return bReached;
}
