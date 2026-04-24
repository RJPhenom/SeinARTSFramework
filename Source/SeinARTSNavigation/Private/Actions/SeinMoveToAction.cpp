/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinMoveToProxy.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Movement/SeinLocomotion.h"
#include "Movement/SeinBasicMovement.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMove, Log, All);

void USeinMoveToAction::Initialize(const FFixedVector& InDestination, FFixedPoint InAcceptanceRadius)
{
	Destination = InDestination;
	AcceptanceRadiusSq = InAcceptanceRadius * InAcceptanceRadius;
	CurrentWaypointIndex = 0;
	bPathResolved = false;
	Path.Clear();
	Locomotion = nullptr;
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

	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);

	// First-tick setup: resolve path + instantiate the locomotion.
	if (!bPathResolved)
	{
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

		// Resolve the locomotion class. Soft class path on FSeinMovementData
		// decouples CoreEntity from SeinARTSNavigation (where the locomotion
		// classes live); TryLoadClass pulls in the resolved UClass* at first use.
		UClass* LocomotionClass = MoveComp->LocomotionClass.IsValid()
			? MoveComp->LocomotionClass.TryLoadClass<USeinLocomotion>()
			: nullptr;
		if (!LocomotionClass || LocomotionClass->HasAnyClassFlags(CLASS_Abstract))
		{
			LocomotionClass = USeinBasicMovement::StaticClass();
		}
		Locomotion = NewObject<USeinLocomotion>(this, LocomotionClass);
		if (Locomotion)
		{
			Locomotion->OnMoveBegin(*Entity, *MoveComp, Path);
		}
	}

	if (!Locomotion)
	{
		// Defensive: should never happen post-bPathResolved, but fail cleanly
		// rather than crash.
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Delegate the per-tick advance. Locomotion mutates Entity.Transform and
	// CurrentWaypointIndex; returns true when the final waypoint is reached.
	const int32 PrevWaypoint = CurrentWaypointIndex;
	const bool bReachedEnd = Locomotion->Tick(
		*Entity, *MoveComp, Path, CurrentWaypointIndex,
		AcceptanceRadiusSq, DeltaTime, Nav);

	// Under-reports if the locomotion consumed multiple waypoints in one tick
	// (only the latest advance fires the notify). Acceptable for MVP; if
	// per-step granularity ever matters, swap to a TFunctionRef callback.
	if (CurrentWaypointIndex > PrevWaypoint)
	{
		NotifyWaypointReached(CurrentWaypointIndex - 1, Path.Waypoints.Num());
	}

	if (bReachedEnd)
	{
		Locomotion->OnMoveEnd(*Entity);
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
