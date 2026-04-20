/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagData.h
 * @brief   Component for gameplay tag management on entities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Components/SeinComponent.h"
#include "SeinTagData.generated.h"

/**
 * Tracks gameplay tags on an entity via reference counting.
 *
 * A tag is "present" (appears in CombinedTags) if and only if its refcount is
 * positive. Multiple sources (archetype BaseTags, abilities, effects, components)
 * can grant the same tag independently; the tag only disappears when the last
 * source releases it. See DESIGN.md §2 for the full invariant.
 *
 * Grant/ungrant operations go through USeinWorldSubsystem so the global
 * EntityTagIndex stays consistent — call FSeinTagData::GrantTagInternal /
 * UngrantTagInternal directly only from subsystem code.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinTagData : public FSeinComponent
{
	GENERATED_BODY()

	/** Designer-authored tags seeded into the refcount map at spawn.
	 *  Runtime-mutable via USeinWorldSubsystem::AddBaseTag/RemoveBaseTag/ReplaceBaseTags.
	 *  Convention: carries archetype-class identity (Unit.Infantry, Unit.Vehicle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Tags")
	FGameplayTagContainer BaseTags;

	/** Live refcount per tag. A tag is present (in CombinedTags) iff its
	 *  refcount is > 0. Keys with refcount 0 are not retained. */
	UPROPERTY()
	TMap<FGameplayTag, int32> TagRefCounts;

	/** Cache of "present" tags for O(1) HasTag queries. Maintained incrementally
	 *  by GrantTagInternal/UngrantTagInternal; rebuilt from TagRefCounts by
	 *  RebuildCombinedTags if a caller mutates BaseTags out-of-band. */
	UPROPERTY()
	FGameplayTagContainer CombinedTags;

	/** Reproject CombinedTags from TagRefCounts (repair routine). */
	void RebuildCombinedTags();

	/** True iff the tag is currently present (refcount > 0). */
	bool HasTag(const FGameplayTag& Tag) const;

	/** True iff any of the given tags is currently present. */
	bool HasAnyTag(const FGameplayTagContainer& Tags) const;

	/** True iff all of the given tags are currently present. */
	bool HasAllTags(const FGameplayTagContainer& Tags) const;

	/** Increment the tag's refcount. Returns true on the 0→1 transition so the
	 *  caller can add the entity to the global EntityTagIndex bucket. */
	bool GrantTagInternal(const FGameplayTag& Tag);

	/** Decrement the tag's refcount. Returns true on the 1→0 transition so the
	 *  caller can remove the entity from the global EntityTagIndex bucket.
	 *  No-op (returns false) on tags that were never granted or have refcount 0. */
	bool UngrantTagInternal(const FGameplayTag& Tag);
};

FORCEINLINE uint32 GetTypeHash(const FSeinTagData& Comp)
{
	uint32 Hash = 0;
	for (const FGameplayTag& Tag : Comp.CombinedTags)
	{
		Hash = HashCombine(Hash, GetTypeHash(Tag));
	}
	return Hash;
}
