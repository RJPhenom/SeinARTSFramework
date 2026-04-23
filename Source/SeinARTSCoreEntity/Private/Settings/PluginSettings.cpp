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
#include "Brokers/SeinDefaultCommandBrokerResolver.h"

USeinARTSCoreSettings::USeinARTSCoreSettings()
	: SimulationTickRate(30)
	, MaxTicksPerFrame(5)
	// ResourceCatalog defaults-constructs via TArray's ctor.
	, DefaultBrokerResolverClass(USeinDefaultCommandBrokerResolver::StaticClass())
	, EffectCountWarningThreshold(256)
	// NavigationClass left empty — subsystem falls back to USeinNavigationAStar
	// when unset. Games pin the desired impl in Project Settings.
	, DefaultCellSize(100.0f)
	, VisionCellSize(400.0f)
	, VisionTickInterval(3)
	, FogRenderTickRate(10.0f)
#if WITH_EDITORONLY_DATA
	, bShowAbilityInBasicCategory(true)
	, bShowComponentInBasicCategory(false)
	, bShowEffectInBasicCategory(true)
	, bShowEntityInBasicCategory(true)
	, bShowWidgetInBasicCategory(false)
#endif
{
	// Vision layers: exactly 6 designer-configurable slots (N0..N5). All start
	// disabled + unnamed — the framework-default "Normal" layer is reserved as
	// the V bit and is always present without needing a slot here. Designers
	// opt in by naming + enabling slots for game-specific channels (Stealth,
	// Thermal, etc.). EditFixedSize prevents add/remove — rename or toggle only.
	VisionLayers.SetNum(6);
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
