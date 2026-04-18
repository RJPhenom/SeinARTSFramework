/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityData.cpp
 * @brief   FSeinAbilityData sim-payload implementation — tag-based lookup
 *          across an entity's granted ability instances.
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
