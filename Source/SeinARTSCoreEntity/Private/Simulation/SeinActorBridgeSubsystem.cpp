/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorBridgeSubsystem.cpp
 * @brief   Actor bridge implementation — spawns actors, syncs transforms,
 *          routes visual events from sim to render layer.
 */

#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBridge, Log, All);

void USeinActorBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SimSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (SimSubsystem.IsValid())
	{
		SimTickDelegateHandle = SimSubsystem->OnSimTickCompleted.AddUObject(
			this, &USeinActorBridgeSubsystem::HandleSimTick);
	}

	UE_LOG(LogSeinBridge, Log, TEXT("SeinActorBridgeSubsystem initialized"));
}

void USeinActorBridgeSubsystem::Deinitialize()
{
	if (SimSubsystem.IsValid())
	{
		SimSubsystem->OnSimTickCompleted.Remove(SimTickDelegateHandle);
	}
	SimTickDelegateHandle.Reset();
	EntityActorMap.Empty();

	Super::Deinitialize();

	UE_LOG(LogSeinBridge, Log, TEXT("SeinActorBridgeSubsystem deinitialized"));
}

TStatId USeinActorBridgeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinActorBridgeSubsystem, STATGROUP_Tickables);
}

// ==================== Tick (Render Frame) ====================

void USeinActorBridgeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SimSubsystem.IsValid())
	{
		return;
	}

	// Flush and dispatch all visual events queued by the sim
	TArray<FSeinVisualEvent> Events = SimSubsystem->FlushVisualEvents();
	for (const FSeinVisualEvent& Event : Events)
	{
		DispatchVisualEvent(Event);
	}
}

// ==================== Sim Tick Callback ====================

void USeinActorBridgeSubsystem::HandleSimTick(int32 Tick)
{
	// Called after each sim tick — tell every managed actor to shift its transform snapshot
	for (auto It = EntityActorMap.CreateIterator(); It; ++It)
	{
		if (It->Value.IsValid())
		{
			ASeinActor* Actor = It->Value.Get();
			if (USeinActorComponent* Comp = Actor->FindComponentByClass<USeinActorComponent>())
			{
				Comp->OnSimTick();
			}
		}
		else
		{
			// Actor was destroyed externally — remove stale entry
			It.RemoveCurrent();
		}
	}
}

// ==================== Visual Event Dispatch ====================

void USeinActorBridgeSubsystem::DispatchVisualEvent(const FSeinVisualEvent& Event)
{
	// Broadcast to global UI listeners before per-actor routing
	OnVisualEventDispatched.Broadcast(Event);

	switch (Event.Type)
	{
	case ESeinVisualEventType::EntitySpawned:
		SpawnActorForEntity(Event.PrimaryEntity, Event);
		break;

	case ESeinVisualEventType::EntityDestroyed:
		HandleEntityDestroyed(Event.PrimaryEntity, Event);
		break;

	case ESeinVisualEventType::TechResearched:
		// Broadcast for UI refresh
		OnTechResearched.Broadcast(Event.PlayerID, Event.Tag);
		break;

	case ESeinVisualEventType::Ping:
		// Ping events are not entity-specific — no actor routing needed.
		// HUD/UI systems should listen for these via a custom delegate or poll.
		break;

	default:
	{
		// Route to the target actor's SeinActorComponent
		TWeakObjectPtr<ASeinActor>* ActorPtr = EntityActorMap.Find(Event.PrimaryEntity);
		if (ActorPtr && ActorPtr->IsValid())
		{
			if (USeinActorComponent* Comp = ActorPtr->Get()->FindComponentByClass<USeinActorComponent>())
			{
				Comp->HandleVisualEvent(Event);
			}
		}
		break;
	}
	}
}

// ==================== Actor Lifecycle ====================

void USeinActorBridgeSubsystem::SpawnActorForEntity(FSeinEntityHandle Handle, const FSeinVisualEvent& SpawnEvent)
{
	if (!SimSubsystem.IsValid()) return;

	// Don't double-spawn
	if (EntityActorMap.Contains(Handle))
	{
		UE_LOG(LogSeinBridge, Warning, TEXT("Actor already exists for entity %s, skipping spawn"), *Handle.ToString());
		return;
	}

	TSubclassOf<ASeinActor> ActorClass = SimSubsystem->GetEntityActorClass(Handle);
	if (!ActorClass)
	{
		UE_LOG(LogSeinBridge, Error, TEXT("No actor class stored for entity %s"), *Handle.ToString());
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// Convert fixed-point spawn location to float for actor placement
	const FVector SpawnLocation = SpawnEvent.Location.ToVector();
	const FRotator SpawnRotation = FRotator::ZeroRotator;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASeinActor* NewActor = World->SpawnActor<ASeinActor>(ActorClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogSeinBridge, Error, TEXT("Failed to spawn actor for entity %s (class: %s)"),
			*Handle.ToString(), *ActorClass->GetName());
		return;
	}

	NewActor->InitializeWithEntity(Handle);
	EntityActorMap.Add(Handle, NewActor);

	UE_LOG(LogSeinBridge, Verbose, TEXT("Spawned actor %s for entity %s"),
		*NewActor->GetName(), *Handle.ToString());
}

void USeinActorBridgeSubsystem::HandleEntityDestroyed(FSeinEntityHandle Handle, const FSeinVisualEvent& DestroyEvent)
{
	TWeakObjectPtr<ASeinActor>* ActorPtr = EntityActorMap.Find(Handle);
	if (!ActorPtr || !ActorPtr->IsValid())
	{
		EntityActorMap.Remove(Handle);
		return;
	}

	ASeinActor* Actor = ActorPtr->Get();

	// Route the destroy event to the actor so it can trigger ReceiveDeath / ReceiveEntityDestroyed
	if (USeinActorComponent* Comp = Actor->FindComponentByClass<USeinActorComponent>())
	{
		Comp->HandleVisualEvent(DestroyEvent);
	}

	// Give the actor time for death animations before cleanup
	Actor->SetLifeSpan(DestroyActorDelay);

	// Remove from our map immediately so we don't route further events to it
	EntityActorMap.Remove(Handle);

	UE_LOG(LogSeinBridge, Verbose, TEXT("Entity %s destroyed — actor %s scheduled for removal in %.1fs"),
		*Handle.ToString(), *Actor->GetName(), DestroyActorDelay);
}

// ==================== Public API ====================

ASeinActor* USeinActorBridgeSubsystem::GetActorForEntity(FSeinEntityHandle Handle) const
{
	const TWeakObjectPtr<ASeinActor>* Found = EntityActorMap.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

void USeinActorBridgeSubsystem::RegisterActor(FSeinEntityHandle Handle, ASeinActor* Actor)
{
	if (!Handle.IsValid() || !Actor)
	{
		UE_LOG(LogSeinBridge, Warning, TEXT("RegisterActor: invalid handle or null actor"));
		return;
	}

	EntityActorMap.Add(Handle, Actor);
}

void USeinActorBridgeSubsystem::UnregisterActor(FSeinEntityHandle Handle)
{
	EntityActorMap.Remove(Handle);
}
