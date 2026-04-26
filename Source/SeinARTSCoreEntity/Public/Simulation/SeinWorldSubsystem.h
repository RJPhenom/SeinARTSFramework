/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.h
 * @brief   World subsystem managing the deterministic simulation.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/EntityID.h"
#include "Types/FixedPoint.h"
#include "Types/Random.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinSpatialHash.h"
#include "Input/SeinCommand.h"
#include "Events/SeinVisualEvent.h"
#include "Components/SeinContainmentTypes.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinVoteState.h"
#include "Data/SeinDiplomacyTypes.h"
#include "SeinWorldSubsystem.generated.h"

class ASeinActor;
class USeinArchetypeDefinition;
class USeinFaction;
class USeinAbility;
class USeinAIController;
class USeinEffect;
class USeinLatentActionManager;
struct FSeinModifier;

/**
 * Scratch record for an effect apply queued during a tick hook. Drained at the
 * next PreTick via `ProcessPendingEffectApplies` — prevents infinite cascades
 * from `OnApply` / `OnTick` hooks per DESIGN §8 Q9c.
 */
struct FSeinPendingEffectApply
{
	FSeinEntityHandle Target;
	TSubclassOf<USeinEffect> EffectClass;
	FSeinEntityHandle Source;
};

/** Broadcast after each sim tick completes (for actor bridge, replay, etc.). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimTickCompleted, int32 /*Tick*/);

/** Broadcast just before commands are processed each tick (for debug logging). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCommandsProcessing, int32 /*Tick*/, const TArray<FSeinCommand>& /*Commands*/);

/**
 * Delegate sim uses to ask "is this target nav-reachable for a Move-like ability?"
 * Registered by USeinNavigationSubsystem (SeinARTSNavigation module) at OnWorldBeginPlay
 * so SeinARTSCoreEntity code can consult it without a circular dependency.
 *
 *   From:      entity's current sim position (fixed-point)
 *   To:        target sim position the command asked to activate against
 *   AgentTags: owning entity's tag container (for navlink eligibility filtering)
 *
 * Return true = target is reachable. False = pre-reject with PathUnreachable.
 * Sim skips the gate if no resolver is registered (tests, nav-less games).
 *
 * Parameters are FFixedVector — not FVector — so the query stays bit-identical
 * across clients. Sim callers already carry FFixedVector, and the nav impl's
 * pathability check is fixed-point throughout.
 */
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FSeinPathableTargetResolver,
	const FFixedVector& /*FromWorld*/, const FFixedVector& /*ToWorld*/, const FGameplayTagContainer& /*AgentTags*/);

/**
 * Delegate sim uses to ask "is this target visible for the owner's VisionGroup?"
 * Registered by USeinFogOfWarSubsystem (SeinARTSFogOfWar) at OnWorldBeginPlay so
 * SeinARTSCoreEntity code can consult it without a circular dependency.
 * Returns true = target is visible. False = reject with NoLineOfSight.
 *
 * Target position is FFixedVector (not FVector) to avoid a lossy float round-
 * trip on the determinism-critical query path — sim callers already carry
 * FFixedVector, and the fog impl's cell lookup is fixed-point throughout.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FSeinLineOfSightResolver,
	FSeinPlayerID /*ObserverPlayer*/, const FFixedVector& /*TargetWorld*/);

/**
 * Delegates sim uses to (un)register entities in the spatial tile grid
 * (DESIGN §13 broadphase). Bound by USeinNavigationSubsystem at
 * OnWorldBeginPlay. Invoked by containment (DESIGN §14) when visibility-
 * mode transitions take an entity off the grid (`Hidden`) or put it back on.
 * Unbound delegates are no-ops — tests and nav-less games skip.
 */
DECLARE_DELEGATE_OneParam(FSeinSpatialGridRegister,   FSeinEntityHandle /*Entity*/);
DECLARE_DELEGATE_OneParam(FSeinSpatialGridUnregister, FSeinEntityHandle /*Entity*/);

/**
 * World subsystem that owns and ticks the deterministic simulation.
 * Manages entity pool, component storage, phase-based tick loop,
 * player states, command processing, and visual event dispatch.
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * GC reference walker. Component payloads live as raw bytes inside
	 * FSeinGenericComponentStorage, so the collector cannot see TObjectPtr /
	 * reflected UObject refs nested in those structs by default. Without this,
	 * ability instances (FSeinAbilityData), broker resolvers (FSeinCommandBrokerData),
	 * and any other designer-authored component holding a UObject ref get
	 * garbage-collected mid-play, leaving dangling pointers that crash on tick.
	 */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// ========== Simulation Control ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StopSimulation();

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationRunning() const { return bIsRunning; }

	/** True iff the sim is paused (DESIGN §17). Paused sim halts the tick
	 *  system loop; commands continue to accumulate in the pending buffer
	 *  (Tactical mode — §18 adds a Hard mode that rejects during pause).
	 *  Visual events still flush so UI can respond to scenario-driven pauses. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationPaused() const { return bSimPaused; }

	/** Sim-pause setter (DESIGN §17 + §18). Pause mode defaults to the match
	 *  settings' `DefaultPauseMode`; pass an explicit value to override. Hard
	 *  pause also sets `bSimPausedHard` so `ProcessCommands` rejects sim-
	 *  mutating commands with `Command.Reject.SimPaused`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void SetSimPaused(bool bPaused, ESeinPauseMode PauseMode = ESeinPauseMode::Tactical);

	// ========== Match Flow (DESIGN §18) ==========

	/** Current match-state-machine state. `Lobby` on Initialize, transitions
	 *  via `StartMatch` / `EndMatch` / pause / etc. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match")
	ESeinMatchState GetMatchState() const { return MatchState; }

	/** Snapshotted match settings (immutable after StartMatch). Reads return
	 *  the captured value so mid-match mutation is impossible by construction. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match")
	const FSeinMatchSettings& GetMatchSettings() const { return CurrentMatchSettings; }

	/**
	 * Start a match with the given settings. Snapshots settings into the
	 * subsystem, transitions state Lobby → Starting, kicks off the
	 * PreMatchCountdown. Calling from any state other than Lobby / Ended
	 * logs a warning + no-ops.
	 *
	 * On countdown completion the subsystem auto-transitions to Playing and
	 * fires `MatchStarted`. Scenarios / UI subscribe to the visual event
	 * stream for those transitions.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match")
	void StartMatch(const FSeinMatchSettings& Settings);

	/** End the match with a declared winner + reason tag. Transitions
	 *  Playing / Paused → Ending → Ended. Scenarios call this when their
	 *  game-specific victory condition fires (DESIGN §18). `Reason` lives
	 *  under designer-authored `MyGame.Victory.*` by convention. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Match")
	void EndMatch(FSeinPlayerID Winner, FGameplayTag Reason);

	// ========== Voting (DESIGN §18) ==========

	/** Open a new vote. No-op (+ warning) if a vote with that tag is already
	 *  active. `ExpiresInTicks <= 0` means no expiration (designer-resolved). */
	void StartVote(FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator);

	/** Register a vote. Re-casts overwrite the prior value. */
	void CastVote(FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue);

	/** Status lookup for UI / scenario gating. */
	ESeinVoteStatus GetVoteStatus(FGameplayTag VoteType) const;

	/** Snapshot of all active votes (for UI listing). */
	TArray<FSeinVoteState> GetActiveVotes() const;

	// ========== Diplomacy (DESIGN §18) ==========

	/** Apply the given add/remove delta to the (From, To) pair. Bypasses the
	 *  match-settings `bAllowMidMatchDiplomacy` gate — intended for framework
	 *  code (ProcessCommands handler checks the gate); direct callers carry
	 *  the responsibility. Updates the parallel tag index + fires the
	 *  DiplomacyChanged event. */
	void ApplyDiplomacyDelta(FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove);

	/** Directional read — empty container if the pair has no authored relation. */
	FGameplayTagContainer GetDiplomacyTags(FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer) const;

	/** Every pair holding this tag (parallel-index O(1) lookup). */
	TArray<FSeinDiplomacyKey> GetPairsWithDiplomacyTag(FGameplayTag Tag) const;

	// ========== CommandBroker helpers (DESIGN §5 — public entry points) ==========

	/** Find or build a broker for a member set + first order. Evicts members
	 *  from any prior broker (one-broker-per-member invariant). Returns the
	 *  broker's entity handle, invalid if post-filter member list is empty.
	 *  Public so framework systems (production rally auto-move, scenario
	 *  orchestration) can dispatch internal broker orders without routing
	 *  through the command buffer. */
	FSeinEntityHandle CreateBrokerForMembers(
		const TArray<FSeinEntityHandle>& FilteredMembers,
		FSeinPlayerID OwnerPlayerID,
		const struct FSeinBrokerQueuedOrder& FirstOrder);

	/** If every member already shares a single broker, return it; else invalid.
	 *  Used to append shift-queued orders to the existing group's broker. */
	FSeinEntityHandle FindSharedBroker(const TArray<FSeinEntityHandle>& Members) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	int32 GetCurrentTick() const { return CurrentTick; }

	/** Fixed delta between sim ticks (seconds). Derived from SimulationTickRate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	float GetFixedDeltaTimeSeconds() const { return FixedDeltaTimeSeconds; }

	/** Interpolation alpha between sim ticks (0-1) for smooth rendering. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	float GetInterpolationAlpha() const;

	// ========== Sim Tick Delegate ==========

	/** Broadcast after each sim tick completes. Used by the actor bridge to sync transforms. */
	FOnSimTickCompleted OnSimTickCompleted;

	/** Broadcast just before ProcessCommands runs, with the pending command buffer. For debug tooling. */
	FOnCommandsProcessing OnCommandsProcessing;

	/** Cross-module resolver for USeinAbility::bRequiresPathableTarget. Registered
	 *  by USeinNavigationSubsystem at OnWorldBeginPlay. */
	FSeinPathableTargetResolver PathableTargetResolver;

	/** Cross-module resolver for USeinAbility::bRequiresLineOfSight. Registered
	 *  by USeinFogOfWarSubsystem at OnWorldBeginPlay. If unbound, LOS checks permit. */
	FSeinLineOfSightResolver LineOfSightResolver;

	/** Cross-module spatial-grid register/unregister callbacks (§13 + §14).
	 *  USeinNavigationSubsystem binds these; containment calls them on
	 *  enter/exit transitions for Hidden-visibility containers. Unbound = no-op. */
	FSeinSpatialGridRegister   SpatialGridRegisterCallback;
	FSeinSpatialGridUnregister SpatialGridUnregisterCallback;

	// ========== Entity Management ==========

	/**
	 * Spawn a new entity from a Blueprint class.
	 * Walks the Blueprint CDO's USeinActorComponent subobjects, calls Resolve()
	 * on each, and copies the returned sim payloads into deterministic storage.
	 * Also initializes abilities and reads identity/cost metadata off the
	 * USeinArchetypeDefinition component.
	 * @param ActorClass - Blueprint class (must be ASeinActor or subclass)
	 * @param SpawnTransform - Initial transform in simulation space
	 * @param OwnerPlayerID - Owning player
	 * @return Handle to the spawned entity
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	FSeinEntityHandle SpawnEntity(TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/**
	 * Spawn a sim entity for an already-existing (level-placed) ASeinActor.
	 * Walks the LIVE actor's USeinActorComponents (not the CDO) so per-
	 * instance edits in the level are captured into sim component storage.
	 * Uses the actor's world transform as the sim transform.
	 *
	 * Skips the EntitySpawned visual event — the actor is already in the
	 * world, so we don't want the bridge spawning a duplicate. Caller is
	 * responsible for `RegisterActor + InitializeWithEntity` to link the
	 * existing actor to the new entity.
	 *
	 * Used by USeinActorBridgeSubsystem::OnWorldBeginPlay to auto-register
	 * placed actors. Returns invalid handle if PlacedActor is null or has
	 * no ArchetypeDefinition.
	 */
	FSeinEntityHandle SpawnEntityFromPlacedActor(ASeinActor* PlacedActor, FSeinPlayerID OwnerPlayerID);

	/**
	 * Spawn an abstract sim entity (no BP class, no render actor). Used for
	 * command brokers, scenario owners, squad containers — anything that needs
	 * pooled handle + component storage but zero render presence. The caller is
	 * responsible for adding whatever sim components it needs via `AddComponent`.
	 * The actor bridge skips handles without a `EntityActorClassMap` entry.
	 */
	FSeinEntityHandle SpawnAbstractEntity(const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/**
	 * Queue entity for deferred destruction (processed in PostTick).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void DestroyEntity(FSeinEntityHandle Handle);

	/** Get entity data by handle. Returns nullptr if handle is stale. */
	FSeinEntity* GetEntity(FSeinEntityHandle Handle);
	const FSeinEntity* GetEntity(FSeinEntityHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool IsEntityAlive(FSeinEntityHandle Handle) const;

	/** Get entity owner. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinPlayerID GetEntityOwner(FSeinEntityHandle Handle) const;

	/** Set entity owner (for capture mechanics). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner);

	/** Get the entity pool (for direct iteration). */
	FSeinEntityPool& GetEntityPool() { return EntityPool; }
	const FSeinEntityPool& GetEntityPool() const { return EntityPool; }

	/** Spatial hash for proximity queries. Rebuilt each tick by
	 *  `FSeinSpatialHashSystem` (PreTick phase). Used by avoidance + future
	 *  cross-cutting systems that need "find entities near point P." */
	FSeinSpatialHash& GetSpatialHash() { return SpatialHash; }
	const FSeinSpatialHash& GetSpatialHash() const { return SpatialHash; }

	/** Get the Blueprint actor class stored for an entity (for actor bridge spawning). */
	TSubclassOf<ASeinActor> GetEntityActorClass(FSeinEntityHandle Handle) const;

	/** Get mutable player state by ID. Returns null if not found. C++ only. */
	FSeinPlayerState* GetPlayerStateMutable(FSeinPlayerID PlayerID);

	// ========== Component Management (slot-indexed) ==========
	//
	// Component types are resolved at spawn time by walking the Blueprint
	// CDO's USeinActorComponents; each one's payload struct is injected into
	// a reflection-backed FSeinGenericComponentStorage keyed by UScriptStruct.
	// Templated accessors are thin typed wrappers over the raw-bytes path.

	template<typename T>
	void AddComponent(FSeinEntityHandle Handle, const T& Component);

	template<typename T>
	void RemoveComponent(FSeinEntityHandle Handle);

	template<typename T>
	T* GetComponent(FSeinEntityHandle Handle);

	template<typename T>
	const T* GetComponent(FSeinEntityHandle Handle) const;

	template<typename T>
	bool HasComponent(FSeinEntityHandle Handle) const;

	/** Get raw component storage by struct type. */
	ISeinComponentStorage* GetComponentStorageRaw(UScriptStruct* StructType);
	const ISeinComponentStorage* GetComponentStorageRaw(UScriptStruct* StructType) const;

	/** Read-only view over every registered storage (UScriptStruct* → storage). Used by
	 *  USeinComponentBPFL for "list every component on entity X" iteration. */
	const TMap<UScriptStruct*, ISeinComponentStorage*>& GetAllComponentStorages() const { return ComponentStorages; }

	// ========== Player & Faction ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/** Get player state by ID. Returns null if not found. C++ only. */
	FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID);
	const FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID) const;

	/** Blueprint-friendly version: returns a copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (DisplayName = "Get Player State"))
	bool GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const;

	/** Iterate every registered player state (mutable). Used by the effect tick
	 *  system to walk per-player effect lists. C++ only. */
	template<typename Func>
	void ForEachPlayerStateMutable(Func&& Callback)
	{
		for (auto& Pair : PlayerStates)
		{
			Callback(Pair.Key, Pair.Value);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterFaction(USeinFaction* Faction);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player")
	int32 GetPlayerCount() const { return PlayerStates.Num(); }

	// ========== Tags (refcounted, auto-indexed) ==========
	//
	// All tag mutations route through the subsystem so the global
	// EntityTagIndex stays in sync with per-entity refcounts. See DESIGN.md §2.

	/** Grant a tag (refcount++). Adds to EntityTagIndex on the 0→1 edge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Ungrant a tag (refcount--). Removes from EntityTagIndex on the 1→0 edge.
	 *  Safe to call on tags that were never granted (no-op). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void UngrantTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Add a tag to BaseTags and grant. Returns true if BaseTags changed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	bool AddBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Remove a tag from BaseTags and ungrant. Returns true if BaseTags changed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	bool RemoveBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag);

	/** Replace the entity's BaseTags. Diffs vs the current set: ungrants removed
	 *  tags, grants new ones, leaves unchanged tags alone. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void ReplaceBaseTags(FSeinEntityHandle Handle, const FGameplayTagContainer& NewBaseTags);

	/** Returns a copy of the entity handles currently carrying the tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags")
	TArray<FSeinEntityHandle> GetEntitiesWithTag(FGameplayTag Tag) const;

	/** Pointer to the underlying index bucket (C++ only; nullptr if tag is absent). */
	const TArray<FSeinEntityHandle>* FindEntitiesWithTag(FGameplayTag Tag) const;

	/** Whole-index access (C++ only, for iteration and debugging). */
	const TMap<FGameplayTag, TArray<FSeinEntityHandle>>& GetEntityTagIndex() const { return EntityTagIndex; }

	// ========== Named Entity Registry ==========

	/** Register an entity under a named alias. Overwrites any existing mapping. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void RegisterNamedEntity(FName Name, FSeinEntityHandle Handle);

	/** Look up an entity by its registered name. Returns an invalid handle if unregistered. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinEntityHandle LookupNamedEntity(FName Name) const;

	/** Remove a named alias. No-op if the name was never registered. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void UnregisterNamedEntity(FName Name);

	// ========== Attribute Resolution ==========

	/**
	 * Resolve an entity attribute with all active Instance + Archetype-scope
	 * modifiers applied. Instance modifiers come from the entity's
	 * `FSeinActiveEffectsData`; Archetype modifiers come from the owner's
	 * `FSeinPlayerState::ArchetypeEffects` (filtered by `TargetArchetypeTag`).
	 * Tech-granted modifiers flow through the same effect pipeline since
	 * Session 2.4 unified tech with effects (DESIGN §10).
	 */
	FFixedPoint ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName);

	/**
	 * Resolve a player-state attribute with all active Player-scope modifiers
	 * applied. Targets fields on `FSeinPlayerState` or designer-authored sub-structs.
	 */
	FFixedPoint ResolvePlayerAttribute(FSeinPlayerID PlayerID, UScriptStruct* StructType, FName FieldName) const;

	// ========== Effects (DESIGN §8) ==========

	/**
	 * Apply an effect to a target entity. If called during a sim tick (from
	 * OnApply/OnTick/OnExpire/OnRemoved), the apply is queued and drained at
	 * the next PreTick. If called outside a sim tick (render-layer authored
	 * ability, test harness), the apply runs immediately.
	 *
	 * Scope from the effect CDO determines where the instance lands:
	 *   Instance → target's FSeinActiveEffectsData
	 *   Archetype → target owner's FSeinPlayerState::ArchetypeEffects
	 *   Player → target owner's FSeinPlayerState::PlayerEffects
	 *
	 * Returns the assigned effect instance ID (0 if the apply failed or was
	 * deferred — deferred applies don't return a usable ID to the caller).
	 */
	uint32 ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	/** Drain the pending-apply queue. Called at PreTick by FSeinEffectTickSystem. */
	void ProcessPendingEffectApplies();

	/** Remove an Instance-scope effect by instance ID. Returns true if removed. */
	bool RemoveInstanceEffect(FSeinEntityHandle Target, uint32 EffectInstanceID, bool bByExpiration);

	/** Remove all Instance-scope effects matching a tag. */
	void RemoveInstanceEffectsWithTag(FSeinEntityHandle Target, FGameplayTag Tag);

	/** Strip every active effect whose `Source == DeadHandle` and whose CDO declares
	 *  `bRemoveOnSourceDeath = true`. Walks all three scope storages (Instance on
	 *  every entity, Archetype/Player on every player state). Called by
	 *  ProcessDeferredDestroys before the pool releases the handle. */
	void RemoveEffectsFromDeadSource(FSeinEntityHandle DeadHandle);

	// ========== Relationships (DESIGN §14) ==========

	/**
	 * Move `Entity` into `Container` as a plain occupant.
	 *
	 * Validates: both handles alive; entity has `FSeinContainmentMemberData`;
	 * container has `FSeinContainmentData`; entity not already contained;
	 * accepted-query match; capacity + Size fits. On success: updates
	 * occupant list, bumps `CurrentLoad`, assigns visual slot if tracking
	 * enabled, unregisters from spatial grid if `Visibility == Hidden`,
	 * emits `EntityEnteredContainer` event.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool EnterContainer(FSeinEntityHandle Entity, FSeinEntityHandle Container);

	/**
	 * Remove `Entity` from its current container. `ExitLocation` zero means
	 * the container's transform + `FSeinTransportSpec::DeployOffset` (if any);
	 * a non-zero vector uses that as the exit world position directly.
	 * Writes the entity's new transform, clears container back-ref, decrements
	 * load, re-registers in spatial grid, emits `EntityExitedContainer`.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool ExitContainer(FSeinEntityHandle Entity, FFixedVector ExitLocation = FFixedVector());

	/**
	 * Attach `Entity` to `Container`'s named `SlotTag` slot. Container must
	 * carry both `FSeinContainmentData` and `FSeinAttachmentSpec`; the slot
	 * must exist in the spec's `Slots` array; the slot must be unfilled; and
	 * the slot's own `AcceptedEntityQuery` must match the entity's tags.
	 * Succeeds after a normal containment enter has been performed (attach
	 * implies containment — the entity occupies both slots + occupant list).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool AttachToSlot(FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag);

	/**
	 * Detach `Entity` from its attachment slot (and exit the container). This
	 * is a combined attachment-detach + container-exit; designers who want to
	 * detach without exiting containment handle that via their own BP logic.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment")
	bool DetachFromSlot(FSeinEntityHandle Entity);

	/**
	 * Propagate container death per DESIGN §14 rules. Called by
	 * `ProcessDeferredDestroys` when a dying entity has `FSeinContainmentData`.
	 *   bEjectOnContainerDeath=true: eject each occupant at container's last
	 *     transform; apply `OnEjectEffect` to each if set; occupants survive.
	 *   bEjectOnContainerDeath=false: apply `OnContainerDeathEffect` to each;
	 *     enqueue each occupant for destroy (recursive — their containments
	 *     propagate too on the next sweep of ProcessDeferredDestroys).
	 */
	void PropagateContainerDeath(FSeinEntityHandle DyingContainer);

	// Introspection — C++ read-only helpers; BPFL wraps.
	FSeinEntityHandle GetImmediateContainer(FSeinEntityHandle Entity) const;
	FSeinEntityHandle GetRootContainer(FSeinEntityHandle Entity) const;
	bool IsContained(FSeinEntityHandle Entity) const;
	TArray<FSeinEntityHandle> GetAllNestedOccupants(FSeinEntityHandle Container) const;
	FSeinContainmentTree BuildContainmentTree(FSeinEntityHandle Container) const;

	// ========== Player Tags (refcounted, DESIGN §10) ==========

	/** Grant a tag to a player (refcount++). Adds to `FSeinPlayerState::PlayerTags`
	 *  on the 0→1 edge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void GrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag);

	/** Ungrant a tag from a player (refcount--). Removes from `PlayerTags` on the
	 *  1→0 edge. Safe to call on tags the player never received (no-op). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags")
	void UngrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag);

	// ========== AI (DESIGN §16) ==========

	/**
	 * Register an AI controller on this world's host. Stamps `OwnedPlayerID` +
	 * `WorldSubsystem`, calls `OnRegistered`, and adds to the tick list. Ticks
	 * fire during CommandProcessing phase so AI-emitted commands process
	 * same-tick.
	 *
	 * In multiplayer, only the designated host should call this — other
	 * clients receive the AI's emitted commands via the lockstep buffer.
	 * V1 does not enforce host-only; designer-side gating. Host migration /
	 * dropped-player takeover plumbing lands with §18 match flow.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI")
	void RegisterAIController(USeinAIController* Controller, FSeinPlayerID OwnedPlayer);

	/** Unregister an AI controller (calls `OnUnregistered`). Safe to call on
	 *  a null / already-unregistered controller. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI")
	void UnregisterAIController(USeinAIController* Controller);

	/** Get the registered AI controller driving `PlayerID`, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|AI")
	USeinAIController* GetAIControllerForPlayer(FSeinPlayerID PlayerID) const;

	/** Read-only view over every registered AI controller. */
	const TArray<TObjectPtr<USeinAIController>>& GetAIControllers() const { return AIControllers; }

	// ========== Command System ==========

	/** Enqueue a command for processing next tick. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command")
	void EnqueueCommand(const FSeinCommand& Command);

	/** Get current tick's pending commands (for networking). */
	const FSeinCommandBuffer& GetPendingCommands() const { return PendingCommands; }

	// ========== System Registration ==========

	/** Register a simulation system to be ticked each frame. */
	void RegisterSystem(ISeinSystem* System);

	/** Unregister a system. */
	void UnregisterSystem(ISeinSystem* System);

	// ========== Visual Events ==========

	/** Enqueue a visual event (sim -> render, one-way). */
	void EnqueueVisualEvent(const FSeinVisualEvent& Event);

	/** Flush all visual events (called by render layer). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Visual")
	TArray<FSeinVisualEvent> FlushVisualEvents();

	// ========== Latent Actions ==========

	UPROPERTY()
	TObjectPtr<USeinLatentActionManager> LatentActionManager;

	// ========== Sim PRNG ==========
	//
	// Framework-owned deterministic RNG used by SeinRollAccuracy and any other
	// sim-side roll that must be replay-identical. Designers who want their own
	// PRNG stream (e.g., per-weapon) make an FFixedRandom of their own; this
	// one is shared-framework scratch.

	FFixedRandom SimRandom;

	// ========== State Hashing ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Debug")
	int32 ComputeStateHash() const;

private:
	// Entity pool (replaces TMap<FSeinID, FSeinEntity>)
	FSeinEntityPool EntityPool;

	// Spatial hash — pure C++, rebuilt each tick by FSeinSpatialHashSystem.
	// Lives next to the entity pool because its lifetime is identical and
	// its callers (sim systems + locomotions) need a stable, world-scoped
	// query surface. Initialized in Initialize().
	FSeinSpatialHash SpatialHash;

	// Component storage registry (slot-indexed, keyed by UScriptStruct*)
	TMap<UScriptStruct*, ISeinComponentStorage*> ComponentStorages;

	// Player states
	TMap<FSeinPlayerID, FSeinPlayerState> PlayerStates;

	// Factions
	TMap<FSeinFactionID, TObjectPtr<USeinFaction>> Factions;

	// Registered systems sorted by phase then priority
	TArray<ISeinSystem*> Systems;
	TArray<ISeinSystem*> BuiltInSystems; // Owned by this subsystem, deleted on deinit
	bool bSystemsSorted = false;

	// Command buffer
	FSeinCommandBuffer PendingCommands;

	// Visual event queue
	FSeinVisualEventQueue VisualEventQueue;

	// Deferred destruction list
	TArray<FSeinEntityHandle> PendingDestroy;

	// Entity → Blueprint class map (for actor bridge spawning)
	TMap<FSeinEntityHandle, TSubclassOf<ASeinActor>> EntityActorClassMap;

	// Global tag → entity index. Maintained by Grant/UngrantTag on 0↔1 refcount
	// transitions. Destroy paths call UnindexEntityTags to clear a handle's
	// buckets before its FSeinTagData storage is freed. See DESIGN.md §2.
	TMap<FGameplayTag, TArray<FSeinEntityHandle>> EntityTagIndex;

	// Named entity registry (designer-addressable aliases).
	TMap<FName, FSeinEntityHandle> NamedEntityRegistry;

	// Pending-apply queue for effects applied during tick hooks. Drained at PreTick.
	TArray<FSeinPendingEffectApply> PendingEffectApplies;

	// AI controller registry (DESIGN §16). Ticked in CommandProcessing phase.
	UPROPERTY()
	TArray<TObjectPtr<USeinAIController>> AIControllers;

	// Tick the registered AI controllers. Called from TickSystems at
	// CommandProcessing phase, right before ProcessCommands.
	void TickAIControllers(FFixedPoint DeltaTime);

	// Commit an apply synchronously (used by ApplyEffect when not in a tick, and
	// by ProcessPendingEffectApplies when draining). Returns instance ID or 0.
	uint32 ApplyEffectInternal(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source);

	// Simulation state
	bool bIsRunning = false;
	bool bSimPaused = false;     // DESIGN §17 — scenario-driven pause gate
	bool bSimPausedHard = false; // DESIGN §18 — true = reject sim-mutating commands while paused
	int32 CurrentTick = 0;
	FTSTicker::FDelegateHandle TickerHandle;

	// Match state (DESIGN §18). State machine drives pre-match countdown, end-match
	// cleanup, spectator + pause filters in ProcessCommands.
	ESeinMatchState MatchState = ESeinMatchState::Lobby;
	FSeinMatchSettings CurrentMatchSettings;
	int32 StartingStateDeadlineTick = 0;   // tick at which Starting → Playing
	int32 MatchStartTick = 0;              // tick at which Playing was entered
	// Advance match state each tick (poll Starting countdown → Playing transition).
	void TickMatchState();

	// Votes (DESIGN §18). Keyed by VoteType. Drained on resolution.
	UPROPERTY()
	TMap<FGameplayTag, FSeinVoteState> ActiveVotes;
	// Tick votes — resolves expired ones at the head of each sim tick.
	void TickVotes();
	// Evaluate a single vote's pass/fail condition and resolve if satisfied.
	// Returns true if the vote was resolved (removed).
	bool EvaluateAndResolveVote(FGameplayTag VoteType);

	// Diplomacy (DESIGN §18). Directional per-pair tag storage + parallel tag→pair
	// index. Maintained together by `ApplyDiplomacyDelta`.
	UPROPERTY()
	TMap<FSeinDiplomacyKey, FGameplayTagContainer> DiplomacyRelations;
	UPROPERTY()
	TMap<FGameplayTag, FSeinDiplomacyIndexBucket> DiplomacyTagIndex;

	// Wall-clock scheduling (NOT sim state — do not "fix" these to FFixedPoint).
	// TimeAccumulator is the render-frame wall-clock budget waiting to be drained
	// into sim ticks; FixedDeltaTimeSeconds is the wall-clock cadence of one tick.
	// The delta actually fed into the sim each tick is a deterministic FFixedPoint
	// derived from SimulationTickRate (see TickSimulation). Clients may drift
	// apart on wall clock between ticks but remain bit-identical at any given
	// tick N, which is what determinism/lockstep requires.
	float TimeAccumulator = 0.0f;
	float FixedDeltaTimeSeconds = 1.0f / 30.0f;

	// Tick pipeline
	bool TickSimulation(float DeltaTime);
	void TickSystems(FFixedPoint DeltaTime);
	void ProcessCommands();
	void ProcessDeferredDestroys();
	void SortSystemsIfNeeded();

	// Archetype to component storage helper
	ISeinComponentStorage* GetOrCreateStorageForType(UScriptStruct* StructType);

	// Ability initialization for spawned entities
	void InitializeEntityAbilities(FSeinEntityHandle Handle);

	// Seed an entity's tag refcounts + EntityTagIndex from its BaseTags (at spawn).
	void SeedEntityTagsFromBase(FSeinEntityHandle Handle);

	// Strip a handle from every EntityTagIndex bucket it appears in (at destroy).
	void UnindexEntityTags(FSeinEntityHandle Handle);

	// Drop any named-registry entries that reference the given handle (at destroy).
	void UnregisterHandleFromNames(FSeinEntityHandle Handle);
};

// ==================== Template Implementations ====================

template<typename T>
void USeinWorldSubsystem::AddComponent(FSeinEntityHandle Handle, const T& Component)
{
	ISeinComponentStorage* Storage = GetOrCreateStorageForType(T::StaticStruct());
	if (Storage)
	{
		Storage->AddComponent(Handle, &Component);
	}
}

template<typename T>
void USeinWorldSubsystem::RemoveComponent(FSeinEntityHandle Handle)
{
	if (ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct()))
	{
		Storage->RemoveComponent(Handle);
	}
}

template<typename T>
T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle)
{
	ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
const T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<const T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
bool USeinWorldSubsystem::HasComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorage* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage && Storage->HasComponent(Handle);
}
