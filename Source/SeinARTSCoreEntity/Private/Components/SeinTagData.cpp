/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinTagComponent.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of tag component methods.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Components/SeinTagData.h"

// ---------------------------------------------------------------------------
// RebuildCombinedTags
// ---------------------------------------------------------------------------

void FSeinTagData::RebuildCombinedTags()
{
	CombinedTags.Reset();
	CombinedTags.AppendTags(BaseTags);
	CombinedTags.AppendTags(GrantedTags);
}

// ---------------------------------------------------------------------------
// HasTag
// ---------------------------------------------------------------------------

bool FSeinTagData::HasTag(const FGameplayTag& Tag) const
{
	return CombinedTags.HasTag(Tag);
}

// ---------------------------------------------------------------------------
// HasAnyTag
// ---------------------------------------------------------------------------

bool FSeinTagData::HasAnyTag(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAny(Tags);
}

// ---------------------------------------------------------------------------
// HasAllTags
// ---------------------------------------------------------------------------

bool FSeinTagData::HasAllTags(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAll(Tags);
}

// ---------------------------------------------------------------------------
// AddGrantedTag
// ---------------------------------------------------------------------------

void FSeinTagData::AddGrantedTag(const FGameplayTag& Tag)
{
	if (Tag.IsValid())
	{
		GrantedTags.AddTag(Tag);
		RebuildCombinedTags();
	}
}

// ---------------------------------------------------------------------------
// RemoveGrantedTag
// ---------------------------------------------------------------------------

void FSeinTagData::RemoveGrantedTag(const FGameplayTag& Tag)
{
	if (Tag.IsValid())
	{
		GrantedTags.RemoveTag(Tag);
		RebuildCombinedTags();
	}
}
