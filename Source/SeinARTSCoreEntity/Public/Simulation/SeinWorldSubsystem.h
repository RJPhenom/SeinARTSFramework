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
#include "Simulation/ComponentStorageV2.h"
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
	 * Reads archetype data from the Blueprint CDO's USeinArchetypeDefinition component,
	 * copies sim components into deterministic storage, and initializes abilities.
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

	// ========== Component Management (V2 - slot indexed) ==========

	template<typename T>
	TSeinComponentStorageV2<T>* RegisterComponentType();

	template<typename T>
	TSeinComponentStorageV2<T>* GetComponentStorage();

	template<typename T>
	const TSeinComponentStorageV2<T>* GetComponentStorage() const;

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

	/** Get raw component storage by struct type (for reflection-based access). */
	ISeinComponentStorageV2* GetComponentStorageRaw(UScriptStruct* StructType);
	const ISeinComponentStorageV2* GetComponentStorageRaw(UScriptStruct* StructType) const;

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

	// Component storage registry (V2: slot-indexed)
	TMap<UScriptStruct*, ISeinComponentStorageV2*> ComponentStorages;

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
	float TimeAccumulator = 0.0f;
	float FixedDeltaTimeSeconds = 1.0f / 30.0f;
	FTSTicker::FDelegateHandle TickerHandle;

	// Tick pipeline
	bool TickSimulation(float DeltaTime);
	void TickSystems(FFixedPoint DeltaTime);
	void ProcessCommands();
	void ProcessDeferredDestroys();
	void SortSystemsIfNeeded();

	// Archetype to component storage helper
	ISeinComponentStorageV2* GetOrCreateStorageForType(UScriptStruct* StructType);

	// Ability initialization for spawned entities
	void InitializeEntityAbilities(FSeinEntityHandle Handle);
};

// ==================== Template Implementations ====================

template<typename T>
TSeinComponentStorageV2<T>* USeinWorldSubsystem::RegisterComponentType()
{
	UScriptStruct* TypeInfo = T::StaticStruct();

	if (ISeinComponentStorageV2** Existing = ComponentStorages.Find(TypeInfo))
	{
		return static_cast<TSeinComponentStorageV2<T>*>(*Existing);
	}

	TSeinComponentStorageV2<T>* Storage = new TSeinComponentStorageV2<T>(EntityPool.GetCapacity());
	ComponentStorages.Add(TypeInfo, Storage);
	return Storage;
}

template<typename T>
TSeinComponentStorageV2<T>* USeinWorldSubsystem::GetComponentStorage()
{
	// Deprecated: the storage backing any given component type at runtime is
	// FSeinGenericComponentStorageV2 (created by GetOrCreateStorageForType),
	// not the templated TSeinComponentStorageV2<T>. static_cast-ing the two
	// is UB — they're sibling classes with different memory layouts.
	// Callers should use GetComponent<T>/HasComponent<T>/AddComponent<T>
	// instead, which route through the virtual ISeinComponentStorageV2 API.
	return nullptr;
}

template<typename T>
const TSeinComponentStorageV2<T>* USeinWorldSubsystem::GetComponentStorage() const
{
	return nullptr;
}

template<typename T>
void USeinWorldSubsystem::AddComponent(FSeinEntityHandle Handle, const T& Component)
{
	ISeinComponentStorageV2* Storage = GetOrCreateStorageForType(T::StaticStruct());
	if (Storage)
	{
		Storage->AddComponent(Handle, &Component);
	}
}

template<typename T>
void USeinWorldSubsystem::RemoveComponent(FSeinEntityHandle Handle)
{
	if (ISeinComponentStorageV2* Storage = GetComponentStorageRaw(T::StaticStruct()))
	{
		Storage->RemoveComponent(Handle);
	}
}

template<typename T>
T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle)
{
	ISeinComponentStorageV2* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
const T* USeinWorldSubsystem::GetComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorageV2* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage ? static_cast<const T*>(Storage->GetComponentRaw(Handle)) : nullptr;
}

template<typename T>
bool USeinWorldSubsystem::HasComponent(FSeinEntityHandle Handle) const
{
	const ISeinComponentStorageV2* Storage = GetComponentStorageRaw(T::StaticStruct());
	return Storage && Storage->HasComponent(Handle);
}
