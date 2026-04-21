/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityData.cpp
 * @brief   FSeinAbilityData sim-payload implementation — tag-based lookup
 *          across an entity's granted ability instances, plus command-context
 *          resolver that picks an ability tag from DefaultCommands.
 */

#include "Components/SeinAbilityData.h"

USeinAbility* FSeinAbilityData::FindAbilityByTag(const FGameplayTag& Tag) const
{
	for (const TObjectPtr<USeinAbility>& Ability : AbilityInstances)
	{
		if (Ability && Ability->AbilityTag == Tag)
		{
			return Ability.Get();
		}
	}
	return nullptr;
}

bool FSeinAbilityData::HasAbilityWithTag(const FGameplayTag& Tag) const
{
	return FindAbilityByTag(Tag) != nullptr;
}

FGameplayTag FSeinAbilityData::ResolveCommandContext(const FGameplayTagContainer& Context) const
{
	// Find the highest-priority mapping whose RequiredContext tags are all present in Context.
	const FSeinCommandMapping* BestMatch = nullptr;

	for (const FSeinCommandMapping& Mapping : DefaultCommands)
	{
		if (!Mapping.AbilityTag.IsValid())
		{
			continue;
		}
		if (!Context.HasAll(Mapping.RequiredContext))
		{
			continue;
		}
		if (!BestMatch || Mapping.Priority > BestMatch->Priority)
		{
			BestMatch = &Mapping;
		}
	}

	return BestMatch ? BestMatch->AbilityTag : FallbackAbilityTag;
}
