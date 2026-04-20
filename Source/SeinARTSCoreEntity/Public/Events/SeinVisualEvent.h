/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisualEvent.h
 * @brief   One-way visual events from simulation to render layer.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinVisualEvent.generated.h"

/**
 * Types of visual events the simulation can emit.
 * The render layer uses these to trigger VFX, audio, UI updates, etc.
 */
UENUM(BlueprintType)
enum class ESeinVisualEventType : uint8
{
	EntitySpawned,
	EntityDestroyed,
	DamageTaken,
	Healed,
	AbilityActivated,
	AbilityEnded,
	EffectApplied,
	EffectRemoved,
	ProductionStarted,
	ProductionCompleted,
	ResourceChanged,
	SquadMemberDied,
	SquadMemberAdded,
	TechResearched,
	Ping,
};

/**
 * A single visual event emitted by the simulation for the render layer to consume.
 * These are strictly one-way: simulation -> render. The render layer never writes back.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisualEvent
{
	GENERATED_BODY()

	/** The type of visual event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	ESeinVisualEventType Type = ESeinVisualEventType::EntitySpawned;

	/** Primary entity involved in this event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinEntityHandle PrimaryEntity;

	/** Secondary entity involved (e.g. damage source, target) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinEntityHandle SecondaryEntity;

	/** Numeric value associated with the event (damage amount, resource delta, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FFixedPoint Value;

	/** Gameplay tag for ability, effect, or archetype identification */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FGameplayTag Tag;

	/** World location relevant to the event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FFixedVector Location;

	/** Player who owns or triggered this event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinPlayerID PlayerID;

	/** Create an entity spawn event */
	static FSeinVisualEvent MakeSpawnEvent(FSeinEntityHandle Entity, FFixedVector Location);

	/** Create an entity destroy event */
	static FSeinVisualEvent MakeDestroyEvent(FSeinEntityHandle Entity);

	/** Create a damage event between source and target */
	static FSeinVisualEvent MakeDamageEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Damage);

	/** Create an ability activation or deactivation event */
	static FSeinVisualEvent MakeAbilityEvent(FSeinEntityHandle Entity, FGameplayTag AbilityTag, bool bActivated);

	/** Create a production started or completed event */
	static FSeinVisualEvent MakeProductionEvent(FSeinEntityHandle Building, FGameplayTag ArchetypeTag, bool bCompleted);

	/** Create an effect applied or removed event */
	static FSeinVisualEvent MakeEffectEvent(FSeinEntityHandle Target, FGameplayTag EffectTag, bool bApplied);

	/** Create a tech researched event (for UI refresh). */
	static FSeinVisualEvent MakeTechResearchedEvent(FSeinPlayerID Player, FGameplayTag TechTag);

	/** Create a ping event at a world location, optionally targeting an entity */
	static FSeinVisualEvent MakePingEvent(FSeinPlayerID Player, FFixedVector Location, FSeinEntityHandle OptionalTarget = FSeinEntityHandle());
};

/**
 * Queue of visual events. The simulation enqueues events during its tick,
 * and the render layer drains them each frame via Flush().
 */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisualEventQueue
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinVisualEvent> Events;

	/** Add an event to the queue */
	void Enqueue(const FSeinVisualEvent& Event);

	/** Return all queued events and clear the queue */
	TArray<FSeinVisualEvent> Flush();

	/** Return the number of events currently queued */
	int32 Num() const;
};
