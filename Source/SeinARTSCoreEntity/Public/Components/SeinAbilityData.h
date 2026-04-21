#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Components/SeinComponent.h"
#include "SeinAbilityData.generated.h"

/**
 * Component tracking an entity's abilities.
 *
 * Holds granted ability classes (from archetype authoring), runtime instances,
 * active primary + passive tracking, and — per DESIGN §7 Q9 — the command
 * resolver (DefaultCommands + FallbackAbilityTag). Designers author command
 * mappings here rather than on the archetype definition; a single edit surface
 * for "which ability fires for which input context."
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityData : public FSeinComponent
{
	GENERATED_BODY()

	/** Ability classes granted to this entity (set from archetype) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<TSubclassOf<USeinAbility>> GrantedAbilityClasses;

	/** Runtime instances (created on entity spawn) */
	UPROPERTY()
	TArray<TObjectPtr<USeinAbility>> AbilityInstances;

	/** Currently active primary ability (not passive) */
	UPROPERTY()
	TObjectPtr<USeinAbility> ActiveAbility;

	/** Active passive abilities */
	UPROPERTY()
	TArray<TObjectPtr<USeinAbility>> ActivePassives;

	/**
	 * Default command mappings for this entity. Maps input contexts (tag sets) to
	 * ability tags. The player controller's smart-command resolver runs these to
	 * pick which ability to activate on right-click. Sorted by priority at resolve
	 * time — highest priority match wins.
	 *
	 * See FSeinCommandMapping for detailed usage examples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<FSeinCommandMapping> DefaultCommands;

	/**
	 * Fallback ability tag when no command mapping matches. Typically
	 * `SeinARTS.Ability.Move` so unmapped contexts default to move.
	 * If empty, no command is issued for unmatched contexts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag FallbackAbilityTag;

	/** Find an ability instance by its AbilityTag */
	USeinAbility* FindAbilityByTag(const FGameplayTag& Tag) const;

	/** Check whether this component has an ability with the given tag */
	bool HasAbilityWithTag(const FGameplayTag& Tag) const;

	/**
	 * Resolve a command context to an ability tag. Walks DefaultCommands in
	 * priority order; returns FallbackAbilityTag if no mapping's RequiredContext
	 * is satisfied.
	 */
	FGameplayTag ResolveCommandContext(const FGameplayTagContainer& Context) const;
};

/** Hash function for deterministic state hashing */
FORCEINLINE uint32 GetTypeHash(const FSeinAbilityData& Comp)
{
	uint32 Hash = 0;
	for (const auto& Ability : Comp.AbilityInstances)
	{
		if (Ability)
		{
			Hash = HashCombine(Hash, GetTypeHash(Ability->CooldownRemaining));
			Hash = HashCombine(Hash, GetTypeHash(Ability->bIsActive));
		}
	}
	return Hash;
}
