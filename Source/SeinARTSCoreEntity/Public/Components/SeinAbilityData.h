#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinComponent.h"
#include "SeinAbilityData.generated.h"

/**
 * Component tracking an entity's abilities.
 * Stores granted ability classes (from archetype) and runtime instances.
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

	/** Find an ability instance by its AbilityTag */
	USeinAbility* FindAbilityByTag(const FGameplayTag& Tag) const;

	/** Check whether this component has an ability with the given tag */
	bool HasAbilityWithTag(const FGameplayTag& Tag) const;
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
