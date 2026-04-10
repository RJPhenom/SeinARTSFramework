/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityViewModel.h
 * @brief   Generic ViewModel providing a read-only lens into any sim entity's
 *          data. Widgets bind to this for real-time display of entity state.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Data/SeinUITypes.h"
#include "GameplayTagContainer.h"
#include "SeinEntityViewModel.generated.h"

class UTexture2D;
class USeinWorldSubsystem;
class USeinActorBridgeSubsystem;
class ASeinActor;
struct FInstancedStruct;

/** Broadcast when the ViewModel has been refreshed with new sim data. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityViewModelRefreshed);

/** Broadcast when the entity this ViewModel tracks has been destroyed or invalidated. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityViewModelInvalidated);

/**
 * Lightweight info struct for a single ability, suitable for UI display.
 * Avoids exposing the full USeinAbility UObject to the render layer.
 */
USTRUCT(BlueprintType)
struct SEINARTSUITOOLKIT_API FSeinAbilityInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	float Cooldown = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	float CooldownRemaining = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsPassive = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	bool bIsOnCooldown = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	TMap<FName, float> ResourceCost;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Ability")
	FGameplayTagContainer AbilityTags;
};

/**
 * Generic ViewModel for any sim entity.
 *
 * Created and cached by USeinUISubsystem. Automatically refreshed each sim tick.
 * Widgets can:
 *   - Bind to BlueprintReadOnly properties via UMG native binding (auto-updates)
 *   - Bind to BlueprintCallable getters via UMG binding functions
 *   - Listen to OnRefreshed for event-driven updates
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinEntityViewModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Identity (cached from archetype, always readable) ==========

	/** The entity handle this ViewModel tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FSeinEntityHandle Entity;

	/** Display name from archetype definition. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FText DisplayName;

	/** Icon texture from archetype definition. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Portrait texture from archetype definition. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	TObjectPtr<UTexture2D> Portrait = nullptr;

	/** Archetype gameplay tag. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FGameplayTag ArchetypeTag;

	/** Owning player ID. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	FSeinPlayerID OwnerPlayerID;

	/** Whether the entity is still alive in the simulation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Entity")
	bool bIsAlive = false;

	// ========== Generic Data Access ==========

	/**
	 * Get a resolved attribute value (base + all modifiers applied).
	 * @param ComponentType - The USTRUCT type of the sim component (e.g., FSeinCombatComponent::StaticStruct())
	 * @param FieldName - The FName of the FFixedPoint field on that struct
	 * @return The resolved value as float, or 0 if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	float GetResolvedAttribute(UScriptStruct* ComponentType, FName FieldName) const;

	/**
	 * Get a base attribute value (no modifiers).
	 * @param ComponentType - The USTRUCT type of the sim component
	 * @param FieldName - The FName of the FFixedPoint field
	 * @return The base value as float, or 0 if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	float GetBaseAttribute(UScriptStruct* ComponentType, FName FieldName) const;

	/** Check if the entity has a specific component type. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	bool HasComponent(UScriptStruct* ComponentType) const;

	/**
	 * Get a full copy of a component's data as an FInstancedStruct.
	 * For advanced Blueprint use where the designer knows the component type.
	 * Returns an empty struct if the component is not present.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FInstancedStruct GetComponentData(UScriptStruct* ComponentType) const;

	/** Get the entity's current gameplay tags. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FGameplayTagContainer GetTags() const;

	// ========== Relationship ==========

	/** Get the relationship between this entity and a specific player. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	ESeinRelation GetRelationToPlayer(FSeinPlayerID PlayerID) const;

	/** Get the relationship between this entity and the local player. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	ESeinRelation GetRelationToLocalPlayer() const;

	// ========== Ability Access ==========

	/** Get info structs for all abilities on this entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	TArray<FSeinAbilityInfo> GetAbilities() const;

	/** Get ability info for a specific ability tag. Returns empty if not found. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	FSeinAbilityInfo GetAbilityByTag(FGameplayTag Tag) const;

	/** Check if the entity has an ability with the given tag. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Entity")
	bool HasAbilityWithTag(FGameplayTag Tag) const;

	// ========== Lifecycle ==========

	/** Fired after Refresh() updates cached data. Widgets bind to this for event-driven updates. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Entity")
	FOnEntityViewModelRefreshed OnRefreshed;

	/** Fired when the tracked entity is destroyed. Widgets should unbind and clear references. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Entity")
	FOnEntityViewModelInvalidated OnInvalidated;

	/**
	 * Initialize this ViewModel with an entity handle.
	 * Called by USeinUISubsystem when creating the ViewModel.
	 * Caches identity data from the archetype definition.
	 */
	void Initialize(FSeinEntityHandle InHandle, USeinWorldSubsystem* InWorldSubsystem);

	/**
	 * Refresh cached data from the simulation.
	 * Called by USeinUISubsystem each sim tick.
	 */
	void Refresh();

	/**
	 * Mark this ViewModel as invalidated (entity destroyed).
	 * Fires OnInvalidated and clears data.
	 */
	void Invalidate();

private:
	/** Cached world subsystem for sim data access. */
	UPROPERTY()
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Build an FSeinAbilityInfo from a USeinAbility instance. */
	FSeinAbilityInfo BuildAbilityInfo(const class USeinAbility* Ability) const;
};
