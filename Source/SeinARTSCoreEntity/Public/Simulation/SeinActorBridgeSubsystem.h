/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorBridgeSubsystem.h
 * @brief   Bridges the deterministic simulation and Unreal's visual layer.
 *          Spawns/destroys actors for sim entities, syncs transforms via
 *          OnSimTick, and routes visual events to actors each render frame.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Events/SeinVisualEvent.h"
#include "SeinActorBridgeSubsystem.generated.h"

class ASeinActor;
class USeinWorldSubsystem;

/** Broadcast when a tech is researched (for UI refresh). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTechResearched, FSeinPlayerID, Player, FGameplayTag, TechTag);

/** Broadcast for every visual event dispatched (for global UI listeners like combat text). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVisualEventDispatched, const FSeinVisualEvent&, Event);

/**
 * World subsystem that bridges the deterministic simulation with Unreal actors.
 *
 * Responsibilities:
 * - Listens to sim tick completion and calls OnSimTick() on all managed actors
 * - Flushes visual events each render frame and dispatches them:
 *     - EntitySpawned → spawns the Blueprint actor, calls InitializeWithEntity
 *     - EntityDestroyed → fires death events, sets actor lifespan for cleanup
 *     - All other events → routes to the target actor's HandleVisualEvent()
 * - Maintains a Handle → Actor map for O(1) lookup
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinActorBridgeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ========== Public API ==========

	/** Get the visual actor for a sim entity. Returns nullptr if no actor exists. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Bridge")
	ASeinActor* GetActorForEntity(FSeinEntityHandle Handle) const;

	/** Manually register an actor for an entity (for pre-placed level actors). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Bridge")
	void RegisterActor(FSeinEntityHandle Handle, ASeinActor* Actor);

	/** Manually unregister an actor from the bridge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Bridge")
	void UnregisterActor(FSeinEntityHandle Handle);

	// ========== Configuration ==========

	/** Delay before destroying an actor after its entity dies (for death animations). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Bridge")
	float DestroyActorDelay = 3.0f;

	// ========== Events ==========

	/** Fired when a tech research completes (for UI systems to refresh). */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Bridge")
	FOnTechResearched OnTechResearched;

	/** Fired for every visual event before per-actor routing. Global UI systems (combat text, notifications) bind to this. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Bridge")
	FOnVisualEventDispatched OnVisualEventDispatched;

private:
	/** Map of entity handles to their visual actors. */
	TMap<FSeinEntityHandle, TWeakObjectPtr<ASeinActor>> EntityActorMap;

	/** Cached pointer to the sim subsystem. */
	TWeakObjectPtr<USeinWorldSubsystem> SimSubsystem;

	/** Delegate handle for sim tick callback. */
	FDelegateHandle SimTickDelegateHandle;

	/** Called after each sim tick — syncs transform snapshots on all actors. */
	void HandleSimTick(int32 Tick);

	/** Process a single visual event. */
	void DispatchVisualEvent(const FSeinVisualEvent& Event);

	/** Spawn a visual actor for an entity. */
	void SpawnActorForEntity(FSeinEntityHandle Handle, const FSeinVisualEvent& SpawnEvent);

	/** Handle entity destroyed event. */
	void HandleEntityDestroyed(FSeinEntityHandle Handle, const FSeinVisualEvent& DestroyEvent);
};
