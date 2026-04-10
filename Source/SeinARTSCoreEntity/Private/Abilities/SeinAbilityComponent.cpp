/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinAbilityComponent.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Ability component implementation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Components/SeinAbilityComponent.h"

USeinAbility* FSeinAbilityComponent::FindAbilityByTag(const FGameplayTag& Tag) const
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

bool FSeinAbilityComponent::HasAbilityWithTag(const FGameplayTag& Tag) const
{
	return FindAbilityByTag(Tag) != nullptr;
}
