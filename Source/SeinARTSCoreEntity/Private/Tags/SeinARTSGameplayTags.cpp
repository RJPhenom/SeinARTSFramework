#include "Tags/SeinARTSGameplayTags.h"

namespace SeinARTSTags
{
	// --- Root ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SeinARTS, "SeinARTS", "Root tag for all SeinARTS Framework tags");

	// --- CommandContext ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_RightClick,      "SeinARTS.CommandContext.RightClick",      "Right mouse button smart command");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Ground,   "SeinARTS.CommandContext.Target.Ground",   "Click target is empty ground");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Friendly, "SeinARTS.CommandContext.Target.Friendly", "Click target is a friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Neutral,  "SeinARTS.CommandContext.Target.Neutral",  "Click target is a neutral entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Enemy,    "SeinARTS.CommandContext.Target.Enemy",    "Click target is an enemy entity");

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
}
