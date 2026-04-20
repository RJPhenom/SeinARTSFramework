/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinArchetypeDefinition.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Component holding archetype data on ASeinActor Blueprints.
 *				Designers edit this on the Blueprint CDO; the sim reads it once
 *				at spawn time and copies into deterministic component storage.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Attributes/SeinModifier.h"
#include "SeinArchetypeDefinition.generated.h"

/**
 * A single mapping from a command context (set of gameplay tags describing the
 * click/input context) to the ability that should be activated.
 *
 * Designers populate an array of these on USeinArchetypeDefinition. When the
 * player right-clicks, the controller builds a context tag set and finds the
 * highest-priority mapping whose RequiredContext is a subset of the actual context.
 *
 * Example for a Medic:
 *   Priority 100: {RightClick, Target.Friendly, Target.Transport} → Ability.Embark
 *   Priority  50: {RightClick, Target.Friendly}                   → Ability.Heal
 *   Priority  50: {RightClick, Target.Enemy}                      → Ability.Attack
 *   Priority   0: {RightClick, Target.Ground}                     → Ability.Movement
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCommandMapping
{
	GENERATED_BODY()

	/** Context tags that must ALL be present for this mapping to match. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTagContainer RequiredContext;

	/** Ability tag to activate when this mapping matches. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTag AbilityTag;

	/** Higher priority mappings are checked first. Most specific mapping should have highest priority. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	int32 Priority = 0;
};

/**
 * Reference to a row in a DataTable that holds component default data.
 * Used by USeinActorComponent::TableOverride to A/B-tune component values
 * across many archetypes via a spreadsheet without leaving the per-component UX.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentTableRef
{
	GENERATED_BODY()

	/** The DataTable containing the component data (row struct must inherit FTableRowBase). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Component")
	TObjectPtr<UDataTable> DataTable;

	/** Row name within the DataTable (e.g., "Rifleman", "Scout"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Component")
	FName RowName;

	bool IsValid() const { return DataTable != nullptr && RowName != NAME_None; }
};

/**
 * Component that defines an entity's archetype data.
 * Lives on ASeinActor as a default subobject. Designers configure this
 * in the Blueprint editor to set up sim components, display info, and icons.
 *
 * This is a pure data container — never ticked, never participates in simulation.
 * The simulation reads from the Blueprint CDO's instance of this component
 * at spawn time, then never references it again.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Archetype Definition"))
class SEINARTSCOREENTITY_API USeinArchetypeDefinition : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinArchetypeDefinition();

	// ========== Identity ==========

	/** Display name for this entity type (shown in UI, production queues, etc.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FText DisplayName;

	/** Description of this entity type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype", meta = (MultiLine = true))
	FText Description;

	/** Icon texture for UI (production buttons, selection panel) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	TObjectPtr<UTexture2D> Icon;

	/** Portrait texture for UI (info panel, veterancy display) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	TObjectPtr<UTexture2D> Portrait;

	/** Gameplay tag uniquely identifying this archetype (e.g., Unit.Infantry, Building.Barracks). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FGameplayTag ArchetypeTag;

	// ========== Production ==========

	/** Resource cost to produce this entity. Keys are resource names (e.g., "Manpower", "Fuel"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	TMap<FName, FFixedPoint> ProductionCost;

	/** Time in sim-seconds to produce this entity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FFixedPoint BuildTime;

	/** Tech tags the owning player must have unlocked to produce/research this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FGameplayTagContainer PrerequisiteTags;

	// ========== Research ==========

	/** If true, completing production grants a tech tag instead of spawning a unit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	bool bIsResearch = false;

	/** Tech tag granted to the player when research completes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype", meta = (EditCondition = "bIsResearch"))
	FGameplayTag GrantedTechTag;

	/** Archetype modifiers granted to the player when research completes (e.g., stat bonuses). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype", meta = (EditCondition = "bIsResearch"))
	TArray<FSeinModifier> GrantedModifiers;

	// ========== Command Resolution ==========

	/**
	 * Default command mappings for this entity type.
	 * Maps input contexts (tag sets) to ability tags. Used by the player controller
	 * to resolve "right-click on X" into the correct ability activation.
	 * Sorted by priority at resolve time — highest priority match wins.
	 *
	 * See FSeinCommandMapping for detailed usage examples.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	TArray<FSeinCommandMapping> DefaultCommands;

	/**
	 * Fallback ability tag when no command mapping matches.
	 * Typically set to Ability.Movement so unmapped contexts default to move.
	 * If empty, no command is issued for unmatched contexts.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FGameplayTag FallbackAbilityTag;

	/**
	 * Resolve a command context to an ability tag using this archetype's mappings.
	 * @param Context - Tags describing the input context (e.g., RightClick + Target.Enemy)
	 * @return The best-matching ability tag, or FallbackAbilityTag if no mapping matches.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Archetype")
	FGameplayTag ResolveCommandContext(const FGameplayTagContainer& Context) const;
};
