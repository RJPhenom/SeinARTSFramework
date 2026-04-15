#include "Tags/SeinARTSGameplayTags.h"

namespace SeinARTSTags
{
	// --- CommandContext ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_RightClick,        "CommandContext.RightClick",        "Right mouse button smart command");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Ground,     "CommandContext.Target.Ground",     "Click target is empty ground");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Friendly,   "CommandContext.Target.Friendly",   "Click target is a friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Neutral,    "CommandContext.Target.Neutral",    "Click target is a neutral entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CommandContext_Target_Enemy,      "CommandContext.Target.Enemy",      "Click target is an enemy entity");

	// --- Ability ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Move,       "Ability.Move",       "Move to a target location");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Stop,       "Ability.Stop",       "Cancel current orders and hold position");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_AttackMove, "Ability.AttackMove", "Move to a location, engaging enemies en route");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack,     "Ability.Attack",     "Attack a specific target");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Build,      "Ability.Build",      "Construct a building or structure");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Repair,     "Ability.Repair",     "Repair a damaged friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Garrison,   "Ability.Garrison",   "Enter a garrisonable structure or transport");
}
