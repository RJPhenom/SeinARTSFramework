#include "Tags/SeinARTSGameplayTags.h"

namespace SeinARTSTags
{
	// --- Root ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SeinARTS, "SeinARTS", "Root tag for all SeinARTS Framework tags");

	// --- Command ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command, "SeinARTS.Command", "Root for command-stream tags (Type / Context / Reject sub-hierarchies)");

	// --- Command.Context ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context,                 "SeinARTS.Command.Context",                 "Root tag for player click-intent descriptors consumed by BuildCommandContext");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_RightClick,      "SeinARTS.Command.Context.RightClick",      "Right mouse button smart command");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Ground,   "SeinARTS.Command.Context.Target.Ground",   "Click target is empty ground");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Friendly, "SeinARTS.Command.Context.Target.Friendly", "Click target is a friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Neutral,  "SeinARTS.Command.Context.Target.Neutral",  "Click target is a neutral entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Enemy,    "SeinARTS.Command.Context.Target.Enemy",    "Click target is an enemy entity");

	// --- Ability ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability,            "SeinARTS.Ability",            "Root tag for ability identities");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Move,       "SeinARTS.Ability.Move",       "Move to a target location");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Stop,       "SeinARTS.Ability.Stop",       "Cancel current orders and hold position");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_AttackMove, "SeinARTS.Ability.AttackMove", "Move to a location, engaging enemies en route");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack,     "SeinARTS.Ability.Attack",     "Attack a specific target");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Build,      "SeinARTS.Ability.Build",      "Construct a building or structure");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Repair,     "SeinARTS.Ability.Repair",     "Repair a damaged friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Garrison,   "SeinARTS.Ability.Garrison",   "Enter a garrisonable structure or transport");

	// --- Unit ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Unit,          "SeinARTS.Unit",          "Root tag for unit classifications");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Unit_Basic,    "SeinARTS.Unit.Basic",    "Basic/default unit classification");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Unit_Infantry, "SeinARTS.Unit.Infantry", "Infantry unit classification");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Unit_Vehicle,  "SeinARTS.Unit.Vehicle",  "Vehicle unit classification");

	// --- Resource ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Resource, "SeinARTS.Resource", "Root tag for designer-defined economy resource identifiers");

	// --- Combat ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(DamageType_Default, "SeinARTS.DamageType.Default", "Starter damage-type tag; designers author their own vocabulary");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(DeathHandler,       "SeinARTS.DeathHandler",       "Classifier tag — abilities carrying this fire on the entity's health-zero event (DESIGN §11)");

	// --- Stats ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat,                       "SeinARTS.Stat",                       "Root tag for attribution stat counters");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_TotalDamageDealt,      "SeinARTS.Stat.TotalDamageDealt",      "Running total of damage dealt by this player's entities");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_TotalDamageReceived,   "SeinARTS.Stat.TotalDamageReceived",   "Running total of damage this player's entities have taken");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_TotalHealsDealt,       "SeinARTS.Stat.TotalHealsDealt",       "Running total of healing dealt by this player");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_UnitKillCount,         "SeinARTS.Stat.UnitKillCount",         "Number of enemy entities killed by this player");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_UnitDeathCount,        "SeinARTS.Stat.UnitDeathCount",        "Number of this player's entities that died");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_UnitsProduced,         "SeinARTS.Stat.UnitsProduced",         "Number of entities successfully produced by this player");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Stat_UnitsLost,             "SeinARTS.Stat.UnitsLost",             "Alias for UnitDeathCount; designers may diverge semantics");

	// --- Command.Type ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type,                   "SeinARTS.Command.Type",                   "Root tag for FSeinCommand command types");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ActivateAbility,   "SeinARTS.Command.Type.ActivateAbility",   "Activate an ability by tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_CancelAbility,     "SeinARTS.Command.Type.CancelAbility",     "Cancel the currently active ability");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_QueueProduction,   "SeinARTS.Command.Type.QueueProduction",   "Queue a unit or research in a production building");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_CancelProduction,  "SeinARTS.Command.Type.CancelProduction",  "Cancel a specific item in the production queue");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_SetRallyPoint,     "SeinARTS.Command.Type.SetRallyPoint",     "Set a building's rally point");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Ping,              "SeinARTS.Command.Type.Ping",              "Ping a location (visible to all players)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer,                        "SeinARTS.Command.Type.Observer",                        "Parent for observer-only commands (logged for replay, skipped by sim)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_CameraUpdate,           "SeinARTS.Command.Type.Observer.CameraUpdate",           "Periodic camera position snapshot");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_SelectionChanged,       "SeinARTS.Command.Type.Observer.SelectionChanged",       "Player changed selection and/or active focus");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ChatMessage,            "SeinARTS.Command.Type.Observer.ChatMessage",            "Player sent a chat message");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Emote,                  "SeinARTS.Command.Type.Observer.Emote",                  "Player triggered a quick-chat emote");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroupAssigned,   "SeinARTS.Command.Type.Observer.ControlGroupAssigned",   "Player assigned a control group");

	// --- Command.Reject ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject,                    "SeinARTS.Command.Reject",                    "Root for CommandRejected reason-tag vocabulary");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_Unaffordable,       "SeinARTS.Command.Reject.Unaffordable",       "SeinCanAfford returned false");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_OnCooldown,         "SeinARTS.Command.Reject.OnCooldown",         "Ability is on cooldown");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_BlockedByTag,       "SeinARTS.Command.Reject.BlockedByTag",       "Entity carries a tag in ActivationBlockedTags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_OutOfRange,         "SeinARTS.Command.Reject.OutOfRange",         "Target is outside the ability's MaxRange");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_InvalidTarget,      "SeinARTS.Command.Reject.InvalidTarget",      "Target fails ValidTargetTags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_NoLineOfSight,      "SeinARTS.Command.Reject.NoLineOfSight",      "Target is not in line of sight (§12)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_PathUnreachable,    "SeinARTS.Command.Reject.PathUnreachable",    "Nav graph has no abstract path from source to goal");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_GoalUnwalkable,     "SeinARTS.Command.Reject.GoalUnwalkable",     "Target cell is blocked / off-map for this agent");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_MissingComponent,   "SeinARTS.Command.Reject.MissingComponent",   "Entity lacks the component the command needs");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_QueueFull,          "SeinARTS.Command.Reject.QueueFull",          "Production queue is at capacity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_CanActivateFailed,  "SeinARTS.Command.Reject.CanActivateFailed",  "USeinAbility::CanActivate BP escape returned false");

	// --- Environment ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Environment_Default, "SeinARTS.Environment.Default", "Default terrain environment tag (designers extend the namespace with biome/surface tags).");
}
