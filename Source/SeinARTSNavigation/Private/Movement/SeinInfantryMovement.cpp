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

DEFINE_LOG_CATEGORY_STATIC(LogSeinLocomotion, Log, All);

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
		const FFixedQuaternion NewQuat = YawOnly(Yaw);
		Entity.Transform.Rotation = NewQuat;
		UE_LOG(LogSeinLocomotion, Verbose,
			TEXT("Infantry: yaw=%.3f rad -> quat(x=%.4f y=%.4f z=%.4f w=%.4f) post-write(x=%.4f y=%.4f z=%.4f w=%.4f)"),
			Yaw.ToFloat(),
			NewQuat.X.ToFloat(), NewQuat.Y.ToFloat(), NewQuat.Z.ToFloat(), NewQuat.W.ToFloat(),
			Entity.Transform.Rotation.X.ToFloat(), Entity.Transform.Rotation.Y.ToFloat(),
			Entity.Transform.Rotation.Z.ToFloat(), Entity.Transform.Rotation.W.ToFloat());
	}
	else
	{
		UE_LOG(LogSeinLocomotion, Verbose,
			TEXT("Infantry: delta too small (sqMag=%.6f), rotation NOT updated"),
			Planar.SizeSquared().ToFloat());
	}

	return bReached;
}
