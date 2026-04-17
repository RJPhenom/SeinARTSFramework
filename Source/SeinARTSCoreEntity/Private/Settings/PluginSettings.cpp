/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		PluginSettings.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of global plugin settings.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Settings/PluginSettings.h"

USeinARTSCoreSettings::USeinARTSCoreSettings()
	: SimulationTickRate(30)
	, MaxTicksPerFrame(5)
#if WITH_EDITORONLY_DATA
	, bShowUnitInBasicCategory(true)
	, bShowAbilityInBasicCategory(true)
	, bShowComponentInBasicCategory(false)
	, bShowWidgetInBasicCategory(false)
#endif
{
}

FName USeinARTSCoreSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText USeinARTSCoreSettings::GetSectionText() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsSection", "SeinARTS");
}

FText USeinARTSCoreSettings::GetSectionDescription() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsDescription",
		"Configure SeinARTS simulation, editor, and content creation settings.");
}
#endif
