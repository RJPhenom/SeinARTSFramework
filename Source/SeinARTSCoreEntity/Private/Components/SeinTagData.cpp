/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagData.cpp
 * @brief   FSeinTagData sim-payload implementation — combined-tag rebuild and
 *          granted-tag mutation helpers.
 */

#include "Components/SeinTagData.h"

void FSeinTagData::RebuildCombinedTags()
{
	CombinedTags.Reset();
	CombinedTags.AppendTags(BaseTags);
	CombinedTags.AppendTags(GrantedTags);
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

void FSeinTagData::AddGrantedTag(const FGameplayTag& Tag)
{
	if (Tag.IsValid())
	{
		GrantedTags.AddTag(Tag);
		RebuildCombinedTags();
	}
}

void FSeinTagData::RemoveGrantedTag(const FGameplayTag& Tag)
{
	if (Tag.IsValid())
	{
		GrantedTags.RemoveTag(Tag);
		RebuildCombinedTags();
	}
}
