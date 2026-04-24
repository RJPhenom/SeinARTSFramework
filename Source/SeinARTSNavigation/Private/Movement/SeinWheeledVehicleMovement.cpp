/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.cpp
 */

#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

bool USeinWheeledVehicleMovement::Tick(
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

	// Waypoint arrival check — same semantics as Basic: one-step radius for
	// mid-path waypoints, AcceptanceRadiusSq for the final one.
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
		// Don't finalize rotation/Z here; the next tick (or Complete) handles
		// remaining work. Avoids a partial-frame visual pop.
		return CurrentWaypointIndex >= Path.Waypoints.Num();
	}

	// Turn: clamp yaw change this tick by TurnRate × DeltaTime, toward the
	// waypoint direction.
	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
	const FFixedPoint DesiredYaw = SeinMath::Atan2(ToTarget.Y, ToTarget.X);
	const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
	const FFixedPoint MaxTurn = MoveData.TurnRate * DeltaTime;
	const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
	const FFixedPoint NewYaw = CurrentYaw + AppliedTurn;
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// Translate along CURRENT forward — not toward the waypoint. That's the
	// key difference from Basic/Infantry: if the vehicle isn't aligned yet,
	// it drives in an arc until it is.
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = OneStep;
	Pos.X = Pos.X + CosY * StepLen;
	Pos.Y = Pos.Y + SinY * StepLen;

	// Snap Z to local nav ground.
	if (Nav)
	{
		FFixedPoint GroundZ;
		if (Nav->GetGroundHeightAt(Pos, GroundZ))
		{
			Pos.Z = GroundZ;
		}
	}

	Entity.Transform.SetLocation(Pos);
	return false;
}
