/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagData.cpp
 * @brief   Refcount-based tag presence tracking for FSeinTagData.
 */

#include "Components/SeinTagData.h"

void FSeinTagData::RebuildCombinedTags()
{
	CombinedTags.Reset();
	for (const TPair<FGameplayTag, int32>& Pair : TagRefCounts)
	{
		if (Pair.Value > 0)
		{
			CombinedTags.AddTag(Pair.Key);
		}
	}
}

bool FSeinTagData::HasTag(const FGameplayTag& Tag) const
{
	return CombinedTags.HasTag(Tag);
}

bool FSeinTagData::HasAnyTag(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAny(Tags);
}

bool FSeinTagData::HasAllTags(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAll(Tags);
}

bool FSeinTagData::GrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return false;
	}

	int32& RefCount = TagRefCounts.FindOrAdd(Tag, 0);
	++RefCount;
	if (RefCount == 1)
	{
		CombinedTags.AddTag(Tag);
		return true;
	}
	return false;
}

bool FSeinTagData::UngrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return false;
	}

	int32* RefCount = TagRefCounts.Find(Tag);
	if (!RefCount || *RefCount <= 0)
	{
		return false;
	}

	--(*RefCount);
	if (*RefCount == 0)
	{
		TagRefCounts.Remove(Tag);
		CombinedTags.RemoveTag(Tag);
		return true;
	}
	return false;
}
