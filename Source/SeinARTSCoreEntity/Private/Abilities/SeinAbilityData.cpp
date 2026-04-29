/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityData.cpp
 * @brief   FSeinAbilityData sim-payload implementation — tag-based lookup
 *          across an entity's granted ability instances, plus command-context
 *          resolver that picks an ability tag from DefaultCommands.
 *
 *          Phase 4 architecture: ability storage is int32 pool IDs, not live
 *          UObject refs. Accessors take a `USeinWorldSubsystem&` and route
 *          through `World.GetAbilityInstance(ID)` for the lookup.
 */

#include "Components/SeinAbilityData.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinAbility* FSeinAbilityData::GetActiveAbility(const USeinWorldSubsystem& World) const
{
	return World.GetAbilityInstance(ActiveAbilityID);
}

TArray<USeinAbility*> FSeinAbilityData::GetAbilityInstances(const USeinWorldSubsystem& World) const
{
	TArray<USeinAbility*> Out;
	Out.Reserve(AbilityInstanceIDs.Num());
	for (int32 ID : AbilityInstanceIDs)
	{
		if (USeinAbility* A = World.GetAbilityInstance(ID))
		{
			Out.Add(A);
		}
	}
	return Out;
}

TArray<USeinAbility*> FSeinAbilityData::GetActivePassives(const USeinWorldSubsystem& World) const
{
	TArray<USeinAbility*> Out;
	Out.Reserve(ActivePassiveIDs.Num());
	for (int32 ID : ActivePassiveIDs)
	{
		if (USeinAbility* A = World.GetAbilityInstance(ID))
		{
			Out.Add(A);
		}
	}
	return Out;
}

USeinAbility* FSeinAbilityData::FindAbilityByTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const
{
	for (int32 ID : AbilityInstanceIDs)
	{
		USeinAbility* A = World.GetAbilityInstance(ID);
		if (A && A->AbilityTag == Tag)
		{
			return A;
		}
	}
	return nullptr;
}

bool FSeinAbilityData::HasAbilityWithTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const
{
	return FindAbilityByTag(World, Tag) != nullptr;
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
