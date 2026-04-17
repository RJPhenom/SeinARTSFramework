/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsData.h
 * @brief   Component that holds all active effects on an entity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Effects/SeinActiveEffect.h"
#include "Attributes/SeinModifier.h"
#include "GameplayTagContainer.h"
#include "Components/SeinComponent.h"
#include "SeinActiveEffectsData.generated.h"

/**
 * ECS-style component that stores the set of active effects on a single entity.
 * Provides methods for adding, removing, querying, and collecting modifiers
 * from all currently active effects.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinActiveEffectsData : public FSeinComponent
{
	GENERATED_BODY()

	/** All currently active effects on this entity. */
	UPROPERTY()
	TArray<FSeinActiveEffect> ActiveEffects;

	/** Monotonically increasing ID counter for effect instances. */
	uint32 NextEffectInstanceID = 1;

	/**
	 * Add an effect. Assigns a unique EffectInstanceID.
	 * @return The assigned EffectInstanceID.
	 */
	uint32 AddEffect(const FSeinActiveEffect& Effect);

	/** Remove an effect by its instance ID. */
	void RemoveEffect(uint32 EffectInstanceID);

	/** Remove all active effects whose EffectTag matches the given tag. */
	void RemoveEffectsWithTag(const FGameplayTag& Tag);

	/** Check whether any active effect has the given EffectTag. */
	bool HasEffectWithTag(const FGameplayTag& Tag) const;

	/** Get the total stack count of effects with the given EffectName. */
	int32 GetStackCount(FName EffectName) const;

	/**
	 * Gather all modifiers from every active effect into OutModifiers.
	 * Modifiers from stacked effects are added once per stack.
	 */
	void CollectModifiers(TArray<FSeinModifier>& OutModifiers) const;

	/** Remove all active effects. */
	void Clear();
};

FORCEINLINE uint32 GetTypeHash(const FSeinActiveEffectsData& Comp)
{
	uint32 Hash = 0;
	for (const FSeinActiveEffect& Effect : Comp.ActiveEffects)
	{
		Hash = HashCombine(Hash, GetTypeHash(Effect));
	}
	return Hash;
}
