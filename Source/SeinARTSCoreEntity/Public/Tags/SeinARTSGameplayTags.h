// Native gameplay tags for the SeinARTS Framework.
//
// All tags consumed by framework C++ should be declared here and defined in
// SeinARTSGameplayTags.cpp. This is the single source of truth for
// framework-shipped tags — add new ones here as systems require them.
//
// Designer/game-specific tags (factions, unit identities, tech trees, terrain
// types, effects, etc.) should live in the project's own tag sources, not
// here. Only tags that the framework itself references belong in this file.
//
// All framework tags live under the "SeinARTS." root to keep them namespaced
// away from project tags.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

namespace SeinARTSTags
{
	// --- Root ---
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SeinARTS);

	// --- CommandContext ---
	// Consumed by ASeinPlayerController::BuildCommandContext to describe the
	// player's click intent. Matched against USeinArchetypeDefinition::DefaultCommands
	// to resolve which ability tag to activate.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CommandContext_RightClick);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CommandContext_Target_Ground);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CommandContext_Target_Friendly);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CommandContext_Target_Neutral);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CommandContext_Target_Enemy);

	// --- Ability ---
	// Standard ability slots the framework knows about. Designers author the
	// actual USeinAbility Blueprints and map these tags in the archetype's
	// DefaultCommands array.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Move);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Stop);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_AttackMove);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Build);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Repair);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Garrison);

	// --- Unit ---
	// Broad unit classification tags — designers extend these in their own
	// tag sources to build out faction/archetype trees.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unit);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unit_Basic);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unit_Infantry);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unit_Vehicle);
}
