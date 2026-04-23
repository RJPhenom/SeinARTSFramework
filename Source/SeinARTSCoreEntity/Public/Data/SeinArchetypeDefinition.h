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
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Data/SeinResourceTypes.h"
#include "Components/SeinProductionData.h"
#include "SeinArchetypeDefinition.generated.h"

class USeinEffect;

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

	/** If true, this entity has no visible actor representation. The actor bridge
	 *  skips spawning a UE actor for it; visual events targeting this entity no-op.
	 *  Use for scenario owners, squad containers, command brokers, and other
	 *  abstract sim-only entities that need storage but no render presence. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	bool bIsAbstract = false;

	// ========== Production ==========

	/** Resource cost to produce this entity. Keyed by SeinARTS.Resource.* tags.
	 *  Cost entries split into AtEnqueue / AtCompletion buckets at queue time based
	 *  on each resource's `ProductionDeductionTiming` in the catalog (§6 + §9). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FSeinResourceCost ProductionCost;

	/** Time in sim-seconds to produce this entity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FFixedPoint BuildTime;

	/** Tech tags the owning player must have unlocked to produce/research this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FGameplayTagContainer PrerequisiteTags;

	/** Refund policy applied when this entry is cancelled mid-build. DESIGN §9 Q2 —
	 *  default progress-proportional refund of the AtEnqueue bucket (AtCompletion
	 *  resources were never deducted and don't refund). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	FSeinProductionRefundPolicy RefundPolicy;

	// ========== Research (DESIGN §10 — tech is an effect) ==========

	/** If true, completing production applies `GrantedTechEffect` to the owning
	 *  player instead of spawning a unit. DESIGN §10 unifies tech with effects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype")
	bool bIsResearch = false;

	/** USeinEffect class applied on research completion. The effect's scope
	 *  (Instance / Archetype / Player) determines where modifiers land; its
	 *  `EffectTag` + `GrantedTags` become player tags (refcounted) per §10. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Archetype",
		meta = (EditCondition = "bIsResearch"))
	TSubclassOf<USeinEffect> GrantedTechEffect;

	// Command-context resolution moved to FSeinAbilityData per DESIGN §7 Q9.
	// Designers author DefaultCommands + FallbackAbilityTag on the abilities
	// component; the archetype no longer carries input-mapping concerns.
};
