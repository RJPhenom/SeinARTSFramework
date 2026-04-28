/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorBridgeSubsystem.cpp
 * @brief   Actor bridge implementation — spawns actors, syncs transforms,
 *          routes visual events from sim to render layer.
 */

#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorBridge.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Events/SeinVisualEvent.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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

void USeinActorBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!bAutoRegisterOnBeginPlay)
	{
		UE_LOG(LogSeinBridge, Log,
			TEXT("OnWorldBeginPlay: auto-register disabled (a match-flow orchestrator owns the bootstrap order)."));
		return;
	}

	RegisterAllPlacedActors(InWorld);
}

void USeinActorBridgeSubsystem::RegisterAllPlacedActors(UWorld& InWorld)
{
	if (!SimSubsystem.IsValid()) return;

	// Collect every ASeinActor in the level, then sort by actor name BEFORE
	// iterating. This is the determinism gate for placed-actor entity-ID
	// assignment: TActorIterator's natural order is implementation-defined
	// and varies between processes, so each PIE window (server + clients)
	// would otherwise hand out different IDs to the same level actors and
	// lockstep would diverge from frame zero. Lexicographic FString sort by
	// AActor::GetName() is stable across processes (the name is the actor's
	// in-level label, identical on every machine for the same level).
	TArray<ASeinActor*> Sorted;
	Sorted.Reserve(64);
	for (TActorIterator<ASeinActor> It(&InWorld); It; ++It)
	{
		if (ASeinActor* A = *It) Sorted.Add(A);
	}
	Sorted.Sort([](const ASeinActor& A, const ASeinActor& B)
	{
		return A.GetName() < B.GetName();
	});

	int32 NumRegistered = 0;
	int32 NumSkipped = 0;
	for (ASeinActor* PlacedActor : Sorted)
	{
		// Skip actors already linked to an entity. Includes the case where
		// the bridge's own SpawnActorForEntity created the actor in
		// response to a runtime SpawnEntity — that path stamps the entity
		// handle on the actor before it ever begin-plays.
		if (PlacedActor->HasValidEntity())
		{
			++NumSkipped;
			continue;
		}

		// Read the per-instance ownership slot set in the level editor.
		// PlayerSlot 0 = neutral (decoration, props, capture points before
		// capture). PlayerSlot N>0 stamps the entity for FSeinPlayerID(N).
		// Note: this fires before any player has connected via
		// HandleStartingNewPlayer, so the player state for slot N may not
		// exist yet — RegisterPlayer creates it later. Anything querying
		// owner state in that window must handle a missing FSeinPlayerState.
		const FSeinPlayerID PlacedOwner = PlacedActor->PlayerSlot > 0
			? FSeinPlayerID(static_cast<uint8>(PlacedActor->PlayerSlot))
			: FSeinPlayerID::Neutral();
		const FSeinEntityHandle Handle = SimSubsystem->SpawnEntityFromPlacedActor(
			PlacedActor, PlacedOwner);
		if (!Handle.IsValid()) continue;

		RegisterActor(Handle, PlacedActor);
		PlacedActor->InitializeWithEntity(Handle);
		++NumRegistered;
	}

	UE_LOG(LogSeinBridge, Log,
		TEXT("RegisterAllPlacedActors: stable-sorted, registered %d placed ASeinActor(s); %d already had entities."),
		NumRegistered, NumSkipped);
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
	// Fixed delta per sim tick. The world subsystem stores this as a float; convert
	// once per broadcast so the hot path stays branch-free.
	const FFixedPoint SimDelta = SimSubsystem.IsValid()
		? FFixedPoint::FromFloat(SimSubsystem->GetFixedDeltaTimeSeconds())
		: FFixedPoint::Zero;

	for (auto It = EntityActorMap.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			// Actor was destroyed externally — remove stale entry.
			It.RemoveCurrent();
			continue;
		}

		ASeinActor* Actor = It->Value.Get();

		// Shift the transform snapshot on the legacy bridge component.
		if (USeinActorBridge* Comp = Actor->FindComponentByClass<USeinActorBridge>())
		{
			Comp->OnSimTick();
		}

		// Fan out to every USeinActorComponent attached to the actor. Each AC
		// short-circuits internally when the BP doesn't override ReceiveSimTick.
		TArray<USeinActorComponent*> SimACs;
		Actor->GetComponents<USeinActorComponent>(SimACs);
		for (USeinActorComponent* AC : SimACs)
		{
			if (AC)
			{
				AC->DispatchSimTick(SimDelta);
			}
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
		// Route to the target actor's legacy bridge AND to every Sein AC attached.
		TWeakObjectPtr<ASeinActor>* ActorPtr = EntityActorMap.Find(Event.PrimaryEntity);
		if (ActorPtr && ActorPtr->IsValid())
		{
			ASeinActor* Actor = ActorPtr->Get();
			if (USeinActorBridge* Comp = Actor->FindComponentByClass<USeinActorBridge>())
			{
				Comp->HandleVisualEvent(Event);
			}

			TArray<USeinActorComponent*> SimACs;
			Actor->GetComponents<USeinActorComponent>(SimACs);
			for (USeinActorComponent* AC : SimACs)
			{
				if (AC)
				{
					AC->DispatchVisualEvent(Event);
				}
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

	// Abstract entities skip actor spawn entirely (per §1 bIsAbstract). Downstream
	// visual events find no actor in EntityActorMap and no-op gracefully.
	const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
	if (CDO && CDO->ArchetypeDefinition && CDO->ArchetypeDefinition->bIsAbstract)
	{
		UE_LOG(LogSeinBridge, Verbose, TEXT("Entity %s is abstract (class %s); skipping actor spawn"),
			*Handle.ToString(), *ActorClass->GetName());
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

	// Fire OnEntitySpawned on every Sein AC so BP subclasses can run their
	// setup scripts once the sim entity is live and components are injected.
	TArray<USeinActorComponent*> SimACs;
	NewActor->GetComponents<USeinActorComponent>(SimACs);
	for (USeinActorComponent* AC : SimACs)
	{
		if (AC)
		{
			AC->DispatchSpawn(Handle);
		}
	}

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
	if (USeinActorBridge* Comp = Actor->FindComponentByClass<USeinActorBridge>())
	{
		Comp->HandleVisualEvent(DestroyEvent);
	}

	// Fan out to every Sein AC before the actor tears down.
	TArray<USeinActorComponent*> SimACs;
	Actor->GetComponents<USeinActorComponent>(SimACs);
	for (USeinActorComponent* AC : SimACs)
	{
		if (AC)
		{
			AC->DispatchDestroy();
		}
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
