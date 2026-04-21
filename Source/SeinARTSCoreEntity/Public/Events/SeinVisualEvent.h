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
	DamageApplied,
	HealApplied,
	Death,
	Kill,
	AbilityActivated,
	AbilityEnded,
	EffectApplied,
	EffectRemoved,
	ProductionStarted,
	ProductionCompleted,
	ProductionStalled,
	ResourceChanged,
	SquadMemberDied,
	SquadMemberAdded,
	TechResearched,
	TechRevoked,
	Ping,
	CommandRejected,
	NavLinkChanged,
	EntityEnteredVision,
	EntityExitedVision,
	TerrainMutated,
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

	/**
	 * Optional reason-tag for CommandRejected events — a gameplay tag under
	 * SeinARTS.Command.Reject.* (Unaffordable / OnCooldown / PathUnreachable /
	 * GoalUnwalkable / etc.) letting UI surface "why" alongside "which command".
	 * Empty for non-CommandRejected events.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FGameplayTag ReasonTag;

	/** Create an entity spawn event */
	static FSeinVisualEvent MakeSpawnEvent(FSeinEntityHandle Entity, FFixedVector Location);

	/** Create an entity destroy event */
	static FSeinVisualEvent MakeDestroyEvent(FSeinEntityHandle Entity);

	/** Create a DamageApplied event (per-hit). `Tag` carries the damage-type tag. */
	static FSeinVisualEvent MakeDamageAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag DamageType);

	/** Create a HealApplied event (per-hit). `Tag` carries the heal-type tag. */
	static FSeinVisualEvent MakeHealAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag HealType);

	/** Create a Death event — entity's health crossed zero. Consumed by UI / render
	 *  for death animation routing; the entity is queued for destroy elsewhere. */
	static FSeinVisualEvent MakeDeathEvent(FSeinEntityHandle Dying, FSeinEntityHandle Killer);

	/** Create a Kill event — counterpart to Death, surfaces the killer side of a
	 *  kill for kill-feed / scoreboard UI. */
	static FSeinVisualEvent MakeKillEvent(FSeinEntityHandle Killer, FSeinEntityHandle Killed, FSeinPlayerID KillerPlayer);

	/** Create an ability activation or deactivation event */
	static FSeinVisualEvent MakeAbilityEvent(FSeinEntityHandle Entity, FGameplayTag AbilityTag, bool bActivated);

	/** Create a production started or completed event */
	static FSeinVisualEvent MakeProductionEvent(FSeinEntityHandle Building, FGameplayTag ArchetypeTag, bool bCompleted);

	/** Create an effect applied or removed event */
	static FSeinVisualEvent MakeEffectEvent(FSeinEntityHandle Target, FGameplayTag EffectTag, bool bApplied);

	/** Create a tech researched event (for UI refresh). */
	static FSeinVisualEvent MakeTechResearchedEvent(FSeinPlayerID Player, FGameplayTag TechTag);

	/** Create a tech revoked event (complement to TechResearched). */
	static FSeinVisualEvent MakeTechRevokedEvent(FSeinPlayerID Player, FGameplayTag TechTag);

	/** Create a production-stalled event (front queue entry at 100% waiting on an
	 *  AtCompletion resource — typically pop cap — to free up). */
	static FSeinVisualEvent MakeProductionStalledEvent(FSeinEntityHandle Building, FGameplayTag ArchetypeTag);

	/** Create a ping event at a world location, optionally targeting an entity */
	static FSeinVisualEvent MakePingEvent(FSeinPlayerID Player, FFixedVector Location, FSeinEntityHandle OptionalTarget = FSeinEntityHandle());

	/** Create a CommandRejected event for UI feedback. `CommandTag` goes in
	 *  `Tag`; optional `ReasonTag` (under SeinARTS.Command.Reject.*) goes in
	 *  the dedicated `ReasonTag` field for richer UI feedback. */
	static FSeinVisualEvent MakeCommandRejectedEvent(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag CommandTag,
		FGameplayTag ReasonTag = FGameplayTag());

	/** Create a NavLinkChanged event — fires when SeinSetNavLinkEnabled toggles
	 *  a link. `Value` encodes the new enabled state as 0 or 1; `Tag` unused. */
	static FSeinVisualEvent MakeNavLinkChangedEvent(int32 NavLinkID, bool bEnabled);

	/** Create a TerrainMutated event — fires when runtime cell mutations occur
	 *  (shelling, footprint register/unregister, nav-tag stamping). `Location` =
	 *  mutation anchor; `Value` = affected-cell count (debug / render hints). */
	static FSeinVisualEvent MakeTerrainMutatedEvent(FFixedVector Location, int32 CellCount);
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
