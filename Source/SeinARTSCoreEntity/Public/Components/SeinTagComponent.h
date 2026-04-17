/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagComponent.h
 * @brief   Component for gameplay tag management on entities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Components/SeinComponent.h"
#include "SeinTagComponent.generated.h"

/**
 * Tracks gameplay tags on an entity. Maintains separate containers for base
 * tags (from the archetype definition) and granted tags (from active effects),
 * with a cached combined container for efficient queries.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinTagComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Tags defined by the entity's archetype (static). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	FGameplayTagContainer BaseTags;

	/** Tags granted by active effects (dynamic). */
	UPROPERTY()
	FGameplayTagContainer GrantedTags;

	/** Cached union of BaseTags and GrantedTags. Rebuilt when tags change. */
	UPROPERTY()
	FGameplayTagContainer CombinedTags;

	/** Rebuild CombinedTags from BaseTags + GrantedTags. */
	void RebuildCombinedTags();

	/** Check if CombinedTags contains the given tag (exact match). */
	bool HasTag(const FGameplayTag& Tag) const;

	/** Check if CombinedTags contains any of the given tags. */
	bool HasAnyTag(const FGameplayTagContainer& Tags) const;

	/** Check if CombinedTags contains all of the given tags. */
	bool HasAllTags(const FGameplayTagContainer& Tags) const;

	/** Add a tag to GrantedTags and rebuild the cache. */
	void AddGrantedTag(const FGameplayTag& Tag);

	/** Remove a tag from GrantedTags and rebuild the cache. */
	void RemoveGrantedTag(const FGameplayTag& Tag);
};

FORCEINLINE uint32 GetTypeHash(const FSeinTagComponent& Comp)
{
	uint32 Hash = 0;
	for (const FGameplayTag& Tag : Comp.CombinedTags)
	{
		Hash = HashCombine(Hash, GetTypeHash(Tag));
	}
	return Hash;
}
