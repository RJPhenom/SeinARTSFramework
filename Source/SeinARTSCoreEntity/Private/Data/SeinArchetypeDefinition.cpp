/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinArchetypeDefinition.cpp
 * @brief:   Archetype identity + metadata. Sim-component authoring lives on
 *           USeinActorComponent subclasses attached to the unit BP; command-
 *           context resolution lives on FSeinAbilityData (per DESIGN §7 Q9).
 *           This component keeps unit-scoped metadata only: identity, cost,
 *           tech, abstract flag, research grants.
 */

#include "Data/SeinArchetypeDefinition.h"

USeinArchetypeDefinition::USeinArchetypeDefinition()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = false;
}
