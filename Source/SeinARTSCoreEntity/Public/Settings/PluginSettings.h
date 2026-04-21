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
#include "Data/SeinResourceTypes.h"
#include "Types/ElevationMode.h"
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

	// Economy Settings — Resource Catalog
	// ====================================================================================================

	/**
	 * Project-wide resource catalog. Each entry declares a resource by gameplay
	 * tag (under SeinARTS.Resource.*) with its display name, icon, caps,
	 * overflow/spend behavior, cost-direction, and production-deduction timing.
	 *
	 * Factions reference catalog entries by tag in their ResourceKit. See
	 * DESIGN.md §6 Resources / §9 Production for the authoritative semantics.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (TitleProperty = "ResourceTag"))
	TArray<FSeinResourceDefinition> ResourceCatalog;

	// Effects
	// ====================================================================================================

	/**
	 * Dev-mode warning threshold for per-entity active-effect count. When an
	 * entity's effect count crosses this threshold (previously below, now at-or-above)
	 * a warning is logged once per crossing — catches runaway apply loops at design
	 * time. Zero runtime cost in shipping; the warning path is compiled out outside
	 * `!UE_BUILD_SHIPPING`. Default: 256.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Effects", meta = (ClampMin = "1", UIMin = "32", UIMax = "1024"))
	int32 EffectCountWarningThreshold;

	// Navigation Settings — consolidated from the old USeinARTSNavigationSettings.
	// ====================================================================================================

	/**
	 * Default cell edge size in world units for ASeinNavVolume bakes (can be
	 * overridden per-volume). Pick a value matching your smallest agent scale —
	 * infantry-centric RTS: ~100 (1 m); massive-unit RTS: ~800 (8 m).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "10.0", UIMin = "50", UIMax = "800"))
	float DefaultCellSize;

	/**
	 * Default elevation mode for new NavVolumes. `None` is the cheapest (4 bytes /
	 * cell); HeightOnly adds a discrete tier; HeightSlope stores quantized slope.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	ESeinElevationMode DefaultElevationMode;

	/**
	 * Default Z separation in world units between stacked layers on multi-layer maps
	 * (overpass + underpass). Per-volume override available. Two walkable-surface hits
	 * closer than this on the same XY column collapse into one layer. Down-facing
	 * hits (ceilings, bridge undersides) are always filtered out before this runs,
	 * so the value only governs *walkable* surface stacking — small thresholds (e.g.
	 * 10 cm) are safe for modern geometry; larger (e.g. 300 cm) helps if you don't
	 * want thin carpet/lip meshes to spawn extra layers on top of a floor.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "1.0", UIMin = "10", UIMax = "1000"))
	float DefaultLayerSeparation;

	/**
	 * Tile size (cells per tile, per axis) for the broadphase tile grid. 32 is a
	 * good balance between summary granularity and skipping benefit. Used by
	 * vision, spatial queries, and HPA* cluster partitioning.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "8", ClampMax = "128"))
	int32 NavTileSize;

	/**
	 * World-unit step between progress toast updates during an async bake.
	 * Coarser = fewer game-thread hops but less responsive UI feedback.
	 * Default 16 tiles per update.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Editor", meta = (ClampMin = "1"))
	int32 BakeTilesPerProgressStep;

	/**
	 * Optional size bins for mixed-scale pathing. When empty, bake produces a
	 * single clearance field and the pathfinder compares `Clearance >= AgentRadius`
	 * for every entity. When populated, bake produces one clearance field per bin
	 * and entities select via `FSeinFootprintData::AgentClass` index.
	 * Values are agent radii in world units.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Agent Classes")
	TArray<float> AgentRadiusClasses;

	/**
	 * Maximum walkable slope angle in degrees for auto-blocking from level
	 * geometry. Hits on surfaces steeper than this are treated as blocked.
	 * Default 45°.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation|Geometry", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxWalkableSlopeDegrees;

	// Editor Settings — Content Browser Factory Visibility
	// ====================================================================================================

#if WITH_EDITORONLY_DATA
	/** If true, the SeinARTS Ability factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Ability in Basic Category"))
	bool bShowAbilityInBasicCategory;

	/** If true, the SeinARTS Component factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Component in Basic Category"))
	bool bShowComponentInBasicCategory;

	/** If true, the SeinARTS Effect factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Effect in Basic Category"))
	bool bShowEffectInBasicCategory;

	/** If true, the SeinARTS Entity Actor factory (Combat is designer-overridden per
	 *  DESIGN §11) appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show SeinARTS Entity Actor in Basic Category"))
	bool bShowEntityInBasicCategory;

	/** If true, the View Model Widget factory appears in the default (Basic) Content Browser category. */
	UPROPERTY(Config, EditAnywhere, Category = "Editor Preferences|Factory Visibility",
		meta = (DisplayName = "Show View Model Widget in Basic Category"))
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
