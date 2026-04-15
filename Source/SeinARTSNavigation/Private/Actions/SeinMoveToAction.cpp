#include "Actions/SeinMoveToAction.h"
#include "SeinPathfinder.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinComponents.h"
#include "Movement/SeinMovementProfile.h"
#include "Movement/SeinInfantryMovementProfile.h"
#include "Components/SeinMovementProfileComponent.h"
#include "Abilities/SeinMoveToProxy.h"

void USeinMoveToAction::Initialize(const FFixedVector& InDestination, USeinPathfinder* InPathfinder, FFixedPoint InAcceptanceRadius)
{
	Destination = InDestination;
	Pathfinder = InPathfinder;
	AcceptanceRadiusSq = InAcceptanceRadius * InAcceptanceRadius;
	CurrentWaypointIndex = 0;
	Path.bIsValid = false;
	KinState = FSeinMovementKinematicState();

	if (!Pathfinder)
	{
		return;
	}
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	FSeinEntity* Entity = World.GetEntity(OwnerEntity);
	if (!Entity)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::EntityDestroyed));
		return true;
	}

	FSeinMovementComponent* MoveComp = World.GetComponent<FSeinMovementComponent>(OwnerEntity);
	if (!MoveComp)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Resolve profile on first tick
	if (!ResolvedProfile)
	{
		UClass* ProfileClass = nullptr;
		if (const FSeinMovementProfileComponent* ProfileComp = World.GetComponent<FSeinMovementProfileComponent>(OwnerEntity))
		{
			ProfileClass = ProfileComp->Profile.Get();
		}
		if (!ProfileClass)
		{
			ProfileClass = USeinInfantryMovementProfile::StaticClass();
		}
		ResolvedProfile = NewObject<USeinMovementProfile>(this, ProfileClass);
	}

	// Request path on first tick
	if (!Path.bIsValid)
	{
		if (!Pathfinder)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::NoPathfinder));
			return true;
		}

		FSeinPathRequest Request;
		Request.Start = Entity->Transform.GetLocation();
		Request.End = Destination;
		Request.Requester = OwnerEntity;

		ResolvedProfile->BuildPath(Pathfinder, Request, Path);

		if (!Path.bIsValid || Path.GetWaypointCount() == 0)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
			return true;
		}
		CurrentWaypointIndex = 0;
	}

	int32 WaypointReached = -1;
	const bool bArrived = ResolvedProfile->AdvanceAlongPath(
		*Entity,
		*MoveComp,
		Path,
		CurrentWaypointIndex,
		Destination,
		AcceptanceRadiusSq,
		DeltaTime,
		KinState,
		WaypointReached);

	// Sync MovementComponent target for visual/steering consumers
	if (CurrentWaypointIndex < Path.GetWaypointCount())
	{
		MoveComp->TargetLocation = Path.Waypoints[CurrentWaypointIndex];
		MoveComp->bHasTarget = true;
	}
	else
	{
		MoveComp->bHasTarget = false;
	}

	if (WaypointReached >= 0)
	{
		NotifyWaypointReached(WaypointReached, Path.GetWaypointCount());
	}

	if (bArrived)
	{
		MoveComp->bHasTarget = false;
		NotifyCompleted();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	if (Observer.IsValid())
	{
		Observer->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	if (Observer.IsValid())
	{
		Observer->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::NotifyCompleted()
{
	if (Observer.IsValid())
	{
		Observer->NotifyCompleted();
	}
}

void USeinMoveToAction::NotifyWaypointReached(int32 Index, int32 Total)
{
	if (Observer.IsValid())
	{
		Observer->NotifyWaypointReached(Index, Total);
	}
}
