/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSettings.cpp
 */

#include "GameMode/SeinWorldSettings.h"

const FSeinMatchSettings& ASeinWorldSettings::GetEffectiveMatchSettings() const
{
	if (MatchSettingsPreset)
	{
		return MatchSettingsPreset->Settings;
	}
	return InlineMatchSettings;
}
