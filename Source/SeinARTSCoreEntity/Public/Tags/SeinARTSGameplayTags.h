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

	// --- Command ---
	// Root namespace for command-stream tags. Three sub-hierarchies:
	//   Command.Context.* — player click-intent descriptors (BuildCommandContext)
	//   Command.Type.*    — FSeinCommand command-type identifiers
	//   Command.Reject.*  — rejection-reason vocabulary attached to CommandRejected
	//                       visual events (DESIGN §13 path-reject plumbing; extended
	//                       during nav refactor Session 3.2).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command);

	// --- Command.Context ---
	// Consumed by ASeinPlayerController::BuildCommandContext to describe the
	// player's click intent. Matched against FSeinAbilityData::DefaultCommands
	// (DESIGN §7 Q9) to resolve which ability tag to activate.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_RightClick);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Ground);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Friendly);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Neutral);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Enemy);

	// --- Ability ---
	// Standard ability slots the framework knows about. Designers author the
	// actual USeinAbility Blueprints and map these tags in the abilities
	// component's DefaultCommands array (FSeinAbilityData, DESIGN §7 Q9).
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

	// --- Resource ---
	// Root for player-economy resource identifiers. Individual resource tags
	// (SeinARTS.Resource.Manpower, .Fuel, etc.) are designer-defined in project
	// tag sources or catalog entries — the framework only ships the root so
	// designer assets can filter pickers to SeinARTS.Resource.* tags.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource);

	// --- Combat ---
	// DESIGN §11: combat is NOT a framework pipeline. Framework only ships the
	// `DamageType.Default` starter and the `DeathHandler` classifier; designers
	// author their own damage-type vocabulary.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DamageType_Default);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DeathHandler);

	// --- Stats ---
	// Attribution counters bumped by framework BPFLs (damage / heal / death /
	// production completion). Designers extend with their own MyGame.Stat.* tags
	// and call USeinStatsBPFL::SeinBumpStat from ability graphs.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_TotalDamageDealt);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_TotalDamageReceived);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_TotalHealsDealt);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_UnitKillCount);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_UnitDeathCount);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_UnitsProduced);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Stat_UnitsLost);

	// --- Command.Type ---
	// Every FSeinCommand carries a CommandType gameplay tag. Sim-side commands
	// live directly under SeinARTS.Command.Type.*; observer commands (logged for
	// replay reconstruction but not processed by the sim) live under
	// SeinARTS.Command.Type.Observer.*. FSeinCommand::IsObserverCommand tests
	// for descendance from the Observer parent tag.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_ActivateAbility);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_CancelAbility);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_QueueProduction);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_CancelProduction);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_SetRallyPoint);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Ping);

	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_CameraUpdate);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_SelectionChanged);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ChatMessage);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Emote);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroupAssigned);

	// --- Command.Reject ---
	// Reason-tag vocabulary attached to CommandRejected visual events (DESIGN.md
	// §13 "Path-reject plumbing"). Designers extend with game-specific reasons
	// by adding their own tags under the parent.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_Unaffordable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_OnCooldown);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_BlockedByTag);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_OutOfRange);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_InvalidTarget);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_NoLineOfSight);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_PathUnreachable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_GoalUnwalkable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_MissingComponent);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_QueueFull);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_CanActivateFailed);

	// --- Environment ---
	// Framework ships `Environment.Default` only; games extend the vocabulary
	// (Environment.Grass, Environment.Snow, etc.) and interpret via effects (DESIGN §13).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Environment_Default);
}
