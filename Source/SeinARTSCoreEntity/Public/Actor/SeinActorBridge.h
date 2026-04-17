/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActorBridge.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Actor component bridging a visual actor to its simulation entity.
 *				Uses generational entity handles, interpolated transform sync,
 *				and forwards visual events from the sim to Blueprint-implementable
 *				events on the owning ASeinActor. Named "Bridge" to distinguish
 *				from FSeinComponent (the sim-side USTRUCT base class).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Transform.h"
#include "SeinActorBridge.generated.h"

class USeinWorldSubsystem;
struct FSeinVisualEvent;

/**
 * Component that bridges a visual actor to its simulation entity.
 * Stores a generational entity handle, interpolates between sim-tick
 * transform snapshots for smooth rendering, and forwards visual events
 * from the simulation to the owning actor.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinActorBridge : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinActorBridge();

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Set the entity handle this component represents.
	 * Takes an initial transform snapshot from the entity.
	 * @param InHandle - Generational entity handle to link to
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void SetEntityHandle(FSeinEntityHandle InHandle);

	/**
	 * Get the entity handle this component represents.
	 * @return Generational entity handle
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinEntityHandle GetEntityHandle() const { return EntityHandle; }

	/**
	 * Check if this component has a valid, living entity.
	 * Validates both handle generation and entity pool liveness.
	 * @return True if handle is valid and entity exists in simulation
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool HasValidEntity() const;

	/**
	 * Enable/disable automatic transform synchronization.
	 * When enabled, actor transform is updated each tick to match simulation.
	 * @param bEnable - Whether to enable sync
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	void SetTransformSyncEnabled(bool bEnable);

	/**
	 * Check if transform sync is enabled.
	 * @return True if sync enabled
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool IsTransformSyncEnabled() const { return bSyncTransform; }

	/**
	 * Called by the subsystem after each simulation tick to capture
	 * transform snapshots for interpolation. Shifts CurrentSimTransform
	 * into PreviousSimTransform, then reads the new current from the entity.
	 */
	void OnSimTick();

	/**
	 * Handle a visual event dispatched from the simulation.
	 * Forwards the event to the owning ASeinActor (if applicable)
	 * so Blueprint-implementable events can fire.
	 */
	void HandleVisualEvent(const FSeinVisualEvent& Event);

protected:
	/** Generational entity handle this component represents */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	FSeinEntityHandle EntityHandle;

	/** Whether to automatically sync actor transform to simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Sync")
	bool bSyncTransform = true;

	/** Whether to interpolate between sim tick snapshots for smooth visuals */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Sync")
	bool bInterpolateTransform = true;

private:
	/** Cached subsystem reference */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> CachedSubsystem;

	/** Transform snapshot from the previous simulation tick */
	FFixedTransform PreviousSimTransform;

	/** Transform snapshot from the most recent simulation tick */
	FFixedTransform CurrentSimTransform;

	/** Whether we have received at least one sim tick snapshot */
	bool bHasSimSnapshot = false;

	/** Get or lazily cache the simulation subsystem */
	USeinWorldSubsystem* GetSubsystem();

	/** Update actor transform from simulation state (with optional interpolation) */
	void SyncTransformToActor();
};
