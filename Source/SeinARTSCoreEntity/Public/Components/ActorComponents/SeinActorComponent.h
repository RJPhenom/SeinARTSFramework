/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinActorComponent.h
 * @brief:   Base class for sim-authoring UActorComponents. Designers attach
 *           subclasses of this to SeinUnit Blueprints; at spawn, the world
 *           subsystem walks the CDO's components, calls Resolve() on each, and
 *           injects the returned FInstancedStruct into deterministic sim storage.
 *
 *           Typed wrappers (USeinMovementComponent, etc.) hold their sim
 *           payload as an inline USTRUCT. USeinStructComponent hosts
 *           designer-authored UDS payloads via the asset broker (see §2).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Events/SeinVisualEvent.h"
#include "Data/SeinArchetypeDefinition.h"
#include "SeinActorComponent.generated.h"

/**
 * Abstract base for all SeinARTS sim-component authoring ActorComponents.
 *
 * Render-side only: the UObject never enters sim storage; only the FInstancedStruct
 * payload returned by Resolve() does. Lifecycle events (OnSpawn/OnSimTick/OnDestroy/
 * OnVisualEvent) run actor-side. They are allowed to read sim state but must never
 * mutate it directly — enqueue a command or activate an ability to change sim state.
 */
UCLASS(Abstract, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinActorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinActorComponent();

	/**
	 * Returns the sim payload injected into deterministic storage at spawn.
	 * Typed wrappers return their inline USTRUCT; the dynamic AC returns an
	 * FInstancedStruct over its compile-synthesised UDS. Default: invalid.
	 */
	virtual FInstancedStruct GetSimComponent() const { return FInstancedStruct(); }

	/**
	 * Optional balance-sheet override. When valid, resolves from the referenced
	 * DataTable row instead of GetSimComponent(). Lets designers A/B-tune many
	 * units from a single spreadsheet without losing the per-component UX.
	 */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Balance")
	FSeinComponentTableRef TableOverride;

	/**
	 * Resolves TableOverride if valid, otherwise falls back to GetSimComponent().
	 * Called by USeinWorldSubsystem::SpawnEntity() on each attached AC.
	 */
	FInstancedStruct Resolve() const;

	// ========== Render-side lifecycle (override in BP) ==========

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Spawned"))
	void ReceiveEntitySpawned(FSeinEntityHandle Handle);

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Sim Tick"))
	void ReceiveSimTick(FFixedPoint DeltaTime);

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Destroyed"))
	void ReceiveEntityDestroyed();

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Visual Event"))
	void ReceiveVisualEvent(const FSeinVisualEvent& Event);

	// Called by USeinActorBridgeSubsystem. No-op if the BP subclass doesn't
	// override the matching Receive* event — override detection is cached on
	// first dispatch so the hot path is a single branch.
	void DispatchSpawn(FSeinEntityHandle Handle);
	void DispatchSimTick(FFixedPoint DeltaTime);
	void DispatchDestroy();
	void DispatchVisualEvent(const FSeinVisualEvent& Event);

private:
	// Resolved on first dispatch: whether the owning class (including BP) overrides
	// each Receive* UFUNCTION. Avoids per-tick FindFunction lookups.
	uint8 bOverrideFlagsResolved : 1;
	uint8 bImplementsSpawn       : 1;
	uint8 bImplementsSimTick     : 1;
	uint8 bImplementsDestroy     : 1;
	uint8 bImplementsVisualEvent : 1;

	void ResolveOverrideFlags();
};
