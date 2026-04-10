/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Base actor class for simulation-backed entities.
 *				Uses generational entity handles and visual event callbacks.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinActor.generated.h"

class USeinActorComponent;
class USeinArchetypeDefinition;

/**
 * Base actor class for entities backed by the deterministic simulation.
 * Designers inherit from this to create unit types, buildings, projectiles, etc.
 * Provides Blueprint-friendly interface for simulation interaction and
 * visual event callbacks (damage, abilities, effects, death).
 */
UCLASS(Blueprintable, BlueprintType)
class SEINARTSCOREENTITY_API ASeinActor : public AActor
{
	GENERATED_BODY()

public:
	ASeinActor();

	// AActor interface
	virtual void BeginPlay() override;

	/**
	 * Initialize this actor with a simulation entity.
	 * Called automatically when spawned from archetype.
	 * @param Handle - Generational entity handle to link to
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	virtual void InitializeWithEntity(FSeinEntityHandle Handle);

	/**
	 * Get the entity handle this actor represents.
	 * @return Generational entity handle, or invalid if not linked
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinEntityHandle GetEntityHandle() const;

	/**
	 * Check if this actor has a valid simulation entity.
	 * @return True if linked to a valid, living entity
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool HasValidEntity() const;

	/** Archetype definition: sim components, display info, icons. Edit on BP defaults. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	TObjectPtr<USeinArchetypeDefinition> ArchetypeDefinition;

protected:
	/** Component linking this actor to simulation entity */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	TObjectPtr<USeinActorComponent> SeinComponent;

public:
	// -- Lifecycle events --

	/** Called when entity is initialized. Override in Blueprints for custom setup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Initialized"))
	void ReceiveEntityInitialized();

	/** Called when entity is destroyed in simulation. Override for cleanup / death effects. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Destroyed"))
	void ReceiveEntityDestroyed();

	// -- Visual events from simulation --
	// These are fired by USeinActorComponent::HandleVisualEvent and are
	// BlueprintImplementableEvent so designers can react in BP subclasses.

	/** Called when this entity takes damage */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Damage Taken"))
	void ReceiveDamageTaken(FFixedPoint Amount, FSeinEntityHandle Source);

	/** Called when this entity is healed */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Healed"))
	void ReceiveHealed(FFixedPoint Amount, FSeinEntityHandle Source);

	/** Called when an ability is activated on this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Ability Activated"))
	void ReceiveAbilityActivated(FGameplayTag AbilityTag);

	/** Called when an ability ends on this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Ability Ended"))
	void ReceiveAbilityEnded(FGameplayTag AbilityTag);

	/** Called when a gameplay effect is applied to this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Effect Applied"))
	void ReceiveEffectApplied(FGameplayTag EffectTag);

	/** Called when a gameplay effect is removed from this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Effect Removed"))
	void ReceiveEffectRemoved(FGameplayTag EffectTag);

	/** Called when this entity dies */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Death"))
	void ReceiveDeath();
};
