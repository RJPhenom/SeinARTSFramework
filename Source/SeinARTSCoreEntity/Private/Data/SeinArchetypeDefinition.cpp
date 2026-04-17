/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinArchetypeDefinition.cpp
 * @brief:   Archetype identity + command resolution. Sim-component authoring
 *           lives on USeinActorComponent subclasses attached to the unit BP;
 *           this class keeps the unit-scoped metadata (display, cost, tech).
 */

#include "Data/SeinArchetypeDefinition.h"

USeinArchetypeDefinition::USeinArchetypeDefinition()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = false;
}

FGameplayTag USeinArchetypeDefinition::ResolveCommandContext(const FGameplayTagContainer& Context) const
{
	// Find the highest-priority mapping whose RequiredContext tags are all present in Context.
	const FSeinCommandMapping* BestMatch = nullptr;

	for (const FSeinCommandMapping& Mapping : DefaultCommands)
	{
		if (!Mapping.AbilityTag.IsValid())
		{
			continue;
		}

		// All tags in RequiredContext must be present in the click context
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
