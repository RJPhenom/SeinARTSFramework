/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinMoveToProxy.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMove, Log, All);

void USeinMoveToAction::Initialize(const FFixedVector& InDestination, FFixedPoint InAcceptanceRadius)
{
	Destination = InDestination;
	AcceptanceRadiusSq = InAcceptanceRadius * InAcceptanceRadius;
	CurrentWaypointIndex = 0;
	bPathResolved = false;
	Path.Clear();
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	FSeinEntity* Entity = World.GetEntity(OwnerEntity);
	if (!Entity)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::EntityDestroyed));
		return true;
	}

	FSeinMovementData* MoveComp = World.GetComponent<FSeinMovementData>(OwnerEntity);
	if (!MoveComp)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Resolve nav + query path once.
	if (!bPathResolved)
	{
		USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);
		if (!Nav)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::NoNavigation));
			return true;
		}

		FSeinPathRequest Req;
		Req.Start = Entity->Transform.GetLocation();
		Req.End = Destination;
		Req.Requester = OwnerEntity;

		if (!Nav->FindPath(Req, Path) || Path.Waypoints.Num() == 0)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
			return true;
		}
		bPathResolved = true;
		CurrentWaypointIndex = 0;
	}

	// Advance along path: seek toward current waypoint at MoveSpeed. Pop waypoints
	// we reach within one step; complete when the final waypoint is within
	// AcceptanceRadius of the entity.
	FFixedVector Pos = Entity->Transform.GetLocation();
	FFixedPoint RemainingStep = MoveComp->MoveSpeed * DeltaTime;

	while (RemainingStep > FFixedPoint::Zero && CurrentWaypointIndex < Path.Waypoints.Num())
	{
		const FFixedVector Target = Path.Waypoints[CurrentWaypointIndex];
		FFixedVector Delta = Target - Pos;
		// Planar distance (ignore Z drift so ramps don't bump the unit up/down
		// via steering — the cell's baked Height is authoritative).
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint DistSq = Delta.SizeSquared();

		const bool bIsFinalWaypoint = (CurrentWaypointIndex == Path.Waypoints.Num() - 1);
		const FFixedPoint ArriveRadiusSq = bIsFinalWaypoint ? AcceptanceRadiusSq : (MoveComp->MoveSpeed * DeltaTime) * (MoveComp->MoveSpeed * DeltaTime);

		if (DistSq <= ArriveRadiusSq)
		{
			// Arrived at this waypoint.
			Pos.X = Target.X;
			Pos.Y = Target.Y;
			Pos.Z = Target.Z;
			NotifyWaypointReached(CurrentWaypointIndex, Path.Waypoints.Num());
			++CurrentWaypointIndex;
			continue;
		}

		// Step toward target, bounded by remaining movement budget this tick.
		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint StepLen = (Dist < RemainingStep) ? Dist : RemainingStep;
		const FFixedVector Dir = FFixedVector::GetSafeNormal(Delta);
		Pos.X = Pos.X + Dir.X * StepLen;
		Pos.Y = Pos.Y + Dir.Y * StepLen;
		Pos.Z = Target.Z; // snap Z to the baked cell height

		RemainingStep = RemainingStep - StepLen;

		// Face movement direction (instant turn — CoH-style infantry feel). Skip
		// when movement is near-zero to avoid flipping on tiny residuals.
		if (Dir.SizeSquared() > FFixedPoint::Epsilon)
		{
			// Yaw only — keep the entity upright regardless of terrain Z drift.
			// Compute yaw from atan2(Dir.Y, Dir.X).
			// TODO: once a fast fixed-point atan2 is wired up we can rotate the
			// quaternion directly. For MVP we leave rotation alone — the visual
			// layer reads velocity for facing in most RTS setups anyway.
		}
	}

	Entity->Transform.SetLocation(Pos);

	// Completed if we walked off the end of the path.
	if (CurrentWaypointIndex >= Path.Waypoints.Num())
	{
		NotifyCompleted();
		Complete();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::NotifyCompleted()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCompleted();
	}
}

void USeinMoveToAction::NotifyWaypointReached(int32 Index, int32 Total)
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyWaypointReached(Index, Total);
	}
}
