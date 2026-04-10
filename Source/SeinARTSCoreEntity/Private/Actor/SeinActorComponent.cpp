/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActorComponent.cpp
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of actor component bridge with interpolation
 *				and visual event forwarding.
 */

#include "Actor/SeinActorComponent.h"
#include "Actor/SeinActor.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Events/SeinVisualEvent.h"
#include "Types/Entity.h"

USeinActorComponent::USeinActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	bSyncTransform = true;
	bInterpolateTransform = true;
	CachedSubsystem = nullptr;
}

void USeinActorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache subsystem reference early
	GetSubsystem();
}

void USeinActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSyncTransform && EntityHandle.IsValid())
	{
		SyncTransformToActor();
	}
}

void USeinActorComponent::SetEntityHandle(FSeinEntityHandle InHandle)
{
	EntityHandle = InHandle;

	if (EntityHandle.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("SeinActorComponent linked to entity %s"), *EntityHandle.ToString());

		// Take initial transform snapshot so interpolation has valid data from frame one
		USeinWorldSubsystem* Subsystem = GetSubsystem();
		if (Subsystem)
		{
			const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
			if (Entity)
			{
				CurrentSimTransform = Entity->Transform;
				PreviousSimTransform = Entity->Transform;
				bHasSimSnapshot = true;
			}
		}
	}
}

bool USeinActorComponent::HasValidEntity() const
{
	if (!EntityHandle.IsValid())
	{
		return false;
	}

	USeinWorldSubsystem* Subsystem = const_cast<USeinActorComponent*>(this)->GetSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	return Subsystem->IsEntityAlive(EntityHandle);
}

void USeinActorComponent::SetTransformSyncEnabled(bool bEnable)
{
	bSyncTransform = bEnable;
}

void USeinActorComponent::OnSimTick()
{
	USeinWorldSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !EntityHandle.IsValid())
	{
		return;
	}

	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity)
	{
		return;
	}

	// Shift current snapshot into previous, then capture new current
	PreviousSimTransform = CurrentSimTransform;
	CurrentSimTransform = Entity->Transform;
	bHasSimSnapshot = true;
}

void USeinActorComponent::HandleVisualEvent(const FSeinVisualEvent& Event)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	ASeinActor* SeinActor = Cast<ASeinActor>(Owner);
	if (!SeinActor)
	{
		return;
	}

	switch (Event.Type)
	{
	case ESeinVisualEventType::DamageTaken:
		SeinActor->ReceiveDamageTaken(Event.Value, Event.SecondaryEntity);
		break;

	case ESeinVisualEventType::Healed:
		SeinActor->ReceiveHealed(Event.Value, Event.SecondaryEntity);
		break;

	case ESeinVisualEventType::AbilityActivated:
		SeinActor->ReceiveAbilityActivated(Event.Tag);
		break;

	case ESeinVisualEventType::AbilityEnded:
		SeinActor->ReceiveAbilityEnded(Event.Tag);
		break;

	case ESeinVisualEventType::EffectApplied:
		SeinActor->ReceiveEffectApplied(Event.Tag);
		break;

	case ESeinVisualEventType::EffectRemoved:
		SeinActor->ReceiveEffectRemoved(Event.Tag);
		break;

	case ESeinVisualEventType::EntityDestroyed:
		SeinActor->ReceiveDeath();
		SeinActor->ReceiveEntityDestroyed();
		break;

	default:
		break;
	}
}

USeinWorldSubsystem* USeinActorComponent::GetSubsystem()
{
	if (!CachedSubsystem)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			CachedSubsystem = World->GetSubsystem<USeinWorldSubsystem>();
		}
	}

	return CachedSubsystem;
}

void USeinActorComponent::SyncTransformToActor()
{
	USeinWorldSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FTransform TargetTransform;

	if (bInterpolateTransform && bHasSimSnapshot)
	{
		// Interpolate between previous and current sim snapshots
		// using the subsystem's interpolation alpha (0 = previous tick, 1 = current tick)
		const float Alpha = Subsystem->GetInterpolationAlpha();

		const FVector PrevLocation = PreviousSimTransform.Location.ToVector();
		const FVector CurrLocation = CurrentSimTransform.Location.ToVector();
		const FVector InterpLocation = FMath::Lerp(PrevLocation, CurrLocation, Alpha);

		const FQuat PrevRotation = PreviousSimTransform.Rotation.ToQuat();
		const FQuat CurrRotation = CurrentSimTransform.Rotation.ToQuat();
		const FQuat InterpRotation = FQuat::Slerp(PrevRotation, CurrRotation, Alpha);

		const FVector PrevScale = PreviousSimTransform.Scale.ToVector();
		const FVector CurrScale = CurrentSimTransform.Scale.ToVector();
		const FVector InterpScale = FMath::Lerp(PrevScale, CurrScale, Alpha);

		TargetTransform = FTransform(InterpRotation, InterpLocation, InterpScale);
	}
	else
	{
		// No interpolation: use the entity's current sim transform directly
		TargetTransform = Entity->Transform.ToTransform();
	}

	Owner->SetActorTransform(TargetTransform);
}
