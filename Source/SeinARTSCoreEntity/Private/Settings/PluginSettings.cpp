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
	, DefaultCellSize(100.0f)
	, DefaultElevationMode(ESeinElevationMode::None)
	, DefaultLayerSeparation(100.0f)
	, NavTileSize(32)
	, BakeTilesPerProgressStep(16)
	, MaxWalkableSlopeDegrees(45.0f)
#if WITH_EDITORONLY_DATA
	, bShowAbilityInBasicCategory(true)
	, bShowComponentInBasicCategory(false)
	, bShowEffectInBasicCategory(true)
	, bShowEntityInBasicCategory(true)
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
