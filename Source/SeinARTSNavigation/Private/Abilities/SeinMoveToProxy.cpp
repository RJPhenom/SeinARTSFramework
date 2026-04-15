#include "Abilities/SeinMoveToProxy.h"
#include "Actions/SeinMoveToAction.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathfinder.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

USeinMoveToProxy* USeinMoveToProxy::SeinMoveTo(USeinAbility* Ability, FFixedVector Destination, FFixedPoint AcceptanceRadius)
{
	USeinMoveToProxy* Proxy = NewObject<USeinMoveToProxy>();
	Proxy->CachedAbility = Ability;
	Proxy->CachedDestination = Destination;
	Proxy->CachedAcceptanceRadius = AcceptanceRadius;
	return Proxy;
}

void USeinMoveToProxy::Activate()
{
	if (!CachedAbility)
	{
		BroadcastFailure(ESeinMoveFailureReason::NoMovementComponent);
		return;
	}

	UWorld* World = CachedAbility->GetWorld();
	if (!World)
	{
		BroadcastFailure(ESeinMoveFailureReason::EntityDestroyed);
		return;
	}

	USeinWorldSubsystem* SimWorld = World->GetSubsystem<USeinWorldSubsystem>();
	USeinNavigationSubsystem* NavSubsystem = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!SimWorld || !NavSubsystem)
	{
		BroadcastFailure(ESeinMoveFailureReason::NoPathfinder);
		return;
	}

	USeinPathfinder* Pathfinder = NavSubsystem->GetPathfinder();
	if (!Pathfinder)
	{
		BroadcastFailure(ESeinMoveFailureReason::NoPathfinder);
		return;
	}

	if (!SimWorld->LatentActionManager)
	{
		BroadcastFailure(ESeinMoveFailureReason::NoPathfinder);
		return;
	}

	USeinMoveToAction* Action = NewObject<USeinMoveToAction>(this);
	Action->OwningAbility = CachedAbility;
	Action->OwnerEntity = CachedAbility->OwnerEntity;
	Action->Observer = this;
	Action->Initialize(CachedDestination, Pathfinder, CachedAcceptanceRadius);

	SimWorld->LatentActionManager->RegisterAction(Action);
	RunningAction = Action;
}

void USeinMoveToProxy::NotifyCompleted()
{
	OnCompleted.Broadcast();
	SetReadyToDestroy();
}

void USeinMoveToProxy::NotifyFailed(ESeinMoveFailureReason Reason)
{
	OnFailed.Broadcast(Reason);
	SetReadyToDestroy();
}

void USeinMoveToProxy::NotifyWaypointReached(int32 Index, int32 Total)
{
	OnWaypointReached.Broadcast(Index, Total);
}

void USeinMoveToProxy::NotifyCancelled()
{
	OnCancelled.Broadcast();
	SetReadyToDestroy();
}

void USeinMoveToProxy::BroadcastFailure(ESeinMoveFailureReason Reason)
{
	OnFailed.Broadcast(Reason);
	SetReadyToDestroy();
}
