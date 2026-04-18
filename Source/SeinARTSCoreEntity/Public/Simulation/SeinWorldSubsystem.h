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
#include "Core/SeinEntityHandle.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/ComponentStorage.h"
#include "Input/SeinCommand.h"
#include "Events/SeinVisualEvent.h"
#include "SeinWorldSubsystem.generated.h"

class ASeinActor;
class USeinArchetypeDefinition;
class USeinFaction;
class USeinAbility;
class USeinLatentActionManager;
struct FSeinModifier;

/** Broadcast after each sim tick completes (for actor bridge, replay, etc.). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimTickCompleted, int32 /*Tick*/);

/** Broadcast just before commands are processed each tick (for debug logging). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCommandsProcessing, int32 /*Tick*/, const TArray<FSeinCommand>& /*Commands*/);

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

	// ========== Simulation Control ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Simulation")
	void StopSimulation();

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Simulation")
	bool IsSimulationRunning() const { return bIsRunning; }

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

	// ========== Player & Faction ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/** Get player state by ID. Returns null if not found. C++ only. */
	FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID);
	const FSeinPlayerState* GetPlayerState(FSeinPlayerID PlayerID) const;

	/** Blueprint-friendly version: returns a copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (DisplayName = "Get Player State"))
	bool GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player")
	void RegisterFaction(USeinFaction* Faction);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player")
	int32 GetPlayerCount() const { return PlayerStates.Num(); }

	// ========== Attribute Resolution ==========

	/**
	 * Resolve an attribute value with all active modifiers applied.
	 * Reads the base value from the entity's component, applies archetype-level
	 * modifiers from the owner's player state, then instance-level modifiers
	 * from the entity's active effects.
	 */
	FFixedPoint ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName);

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

	// ========== State Hashing ==========

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Debug")
	int32 ComputeStateHash() const;

private:
	// Entity pool (replaces TMap<FSeinID, FSeinEntity>)
	FSeinEntityPool EntityPool;

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

	// Simulation state
	bool bIsRunning = false;
	int32 CurrentTick = 0;
	FTSTicker::FDelegateHandle TickerHandle;

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
