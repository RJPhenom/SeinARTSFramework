/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		PluginSettings.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Global plugin settings for SeinARTS.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PluginSettings.generated.h"

/**
 * Global settings for SeinARTS.
 * Configure these in Project Settings > Plugins > SeinARTS.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SeinARTS"))
class SEINARTSCOREENTITY_API USeinARTSCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USeinARTSCoreSettings();

	// Simulation Settings
	// ====================================================================================================

	/**
	 * Simulation tick rate (ticks per second).
	 * Higher tick rates = smoother simulation but higher CPU cost.
	 * Default: 30 ticks per second.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "120", UIMin = "10", UIMax = "60"))
	int32 SimulationTickRate;

	/**
	 * Maximum number of simulation ticks to process per frame.
	 * Prevents "spiral of death" when frame rate drops below tick rate.
	 * Default: 5 ticks per frame maximum.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "30", UIMin = "1", UIMax = "10"))
	int32 MaxTicksPerFrame;

	// Editor Settings — Content Browser Factory Visibility
	// ====================================================================================================

#if WITH_EDITORONLY_DATA
	/** If true, the Unit factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Content Browser|Factory Visibility")
	bool bShowUnitInBasicCategory;

	/** If true, the Ability factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Content Browser|Factory Visibility")
	bool bShowAbilityInBasicCategory;

	/** If true, the Component factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Content Browser|Factory Visibility")
	bool bShowComponentInBasicCategory;

	/** If true, the Widget factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Content Browser|Factory Visibility")
	bool bShowWidgetInBasicCategory;
#endif

	// UDeveloperSettings Interface
	// ====================================================================================================

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
