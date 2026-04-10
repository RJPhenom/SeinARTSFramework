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

#include "Components/SeinTagComponent.h"

// ---------------------------------------------------------------------------
// RebuildCombinedTags
// ---------------------------------------------------------------------------

void FSeinTagComponent::RebuildCombinedTags()
{
	CombinedTags.Reset();
	CombinedTags.AppendTags(BaseTags);
	CombinedTags.AppendTags(GrantedTags);
}

// ---------------------------------------------------------------------------
// HasTag
// ---------------------------------------------------------------------------

bool FSeinTagComponent::HasTag(const FGameplayTag& Tag) const
{
	return CombinedTags.HasTag(Tag);
}

// ---------------------------------------------------------------------------
// HasAnyTag
// ---------------------------------------------------------------------------

bool FSeinTagComponent::HasAnyTag(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAny(Tags);
}

// ---------------------------------------------------------------------------
// HasAllTags
// ---------------------------------------------------------------------------

bool FSeinTagComponent::HasAllTags(const FGameplayTagContainer& Tags) const
{
	return CombinedTags.HasAll(Tags);
}

// ---------------------------------------------------------------------------
// AddGrantedTag
// ---------------------------------------------------------------------------

void FSeinTagComponent::AddGrantedTag(const FGameplayTag& Tag)
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

void FSeinTagComponent::RemoveGrantedTag(const FGameplayTag& Tag)
{
	if (Tag.IsValid())
	{
		GrantedTags.RemoveTag(Tag);
		RebuildCombinedTags();
	}
}
