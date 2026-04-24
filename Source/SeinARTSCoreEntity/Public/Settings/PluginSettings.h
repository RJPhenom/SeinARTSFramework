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
#include "UObject/SoftObjectPath.h"
#include "Data/SeinResourceTypes.h"
#include "Data/SeinVisionLayerDefinition.h"
#include "PluginSettings.generated.h"

class USeinCommandBrokerResolver;

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

	// Command Brokers (DESIGN §5)
	// ====================================================================================================

	/**
	 * Default resolver class instantiated for every spawned CommandBroker. Framework
	 * ships `USeinDefaultCommandBrokerResolver` (capability-map filtered + uniform
	 * grid positions); designers override here to ship a project-wide dispatch
	 * policy (tight ranks, class clusters, wedge formations, etc.). If empty, the
	 * framework default is used.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Command Brokers",
		meta = (DisplayName = "Default Broker Resolver Class"))
	TSoftClassPtr<USeinCommandBrokerResolver> DefaultBrokerResolverClass;

	/**
	 * Dev-mode warning threshold for per-entity active-effect count. When an
	 * entity's effect count crosses this threshold (previously below, now at-or-above)
	 * a warning is logged once per crossing — catches runaway apply loops at design
	 * time. Zero runtime cost in shipping; the warning path is compiled out outside
	 * `!UE_BUILD_SHIPPING`. Default: 256.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Effects", meta = (ClampMin = "1", UIMin = "32", UIMax = "1024"))
	int32 EffectCountWarningThreshold;

	// Navigation Settings (DESIGN §13)
	// ====================================================================================================

	/**
	 * Active navigation implementation. The framework's MoveTo action, editor
	 * bake button, and ability validation all route through this class — the
	 * rest of the plugin is wholly decoupled from nav semantics.
	 *
	 * Ships with `USeinNavigationAStar` as the default: single-layer 2D grid +
	 * synchronous A* + line-of-sight smoothing, suitable as a minimal reference
	 * and for small-to-medium RTS maps. Game teams can subclass or replace
	 * entirely without touching any framework code.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation",
		meta = (DisplayName = "Navigation Class", MetaClass = "/Script/SeinARTSNavigation.SeinNavigation"))
	FSoftClassPath NavigationClass;

	/**
	 * Default cell edge size in world units for ASeinNavVolume bakes (per-
	 * volume overrides supported). Pick a value matching your smallest agent
	 * scale — infantry-centric RTS: ~100 (1 m); massive-unit RTS: ~800 (8 m).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "10.0", UIMin = "50", UIMax = "800"))
	float DefaultCellSize;

	// Vision / Fog of War Settings
	// ====================================================================================================

	/**
	 * Active fog-of-war implementation. The framework's reader BPFL, editor
	 * bake button, and cross-module LOS resolver route through this class —
	 * the rest of the plugin is wholly decoupled from fog-of-war semantics.
	 *
	 * Ships with `USeinFogOfWarDefault` as the default: single-layer 2D grid
	 * with symmetric-shadowcast visibility + lampshade height model (CoH
	 * TrueSight behavior). Game teams can subclass or replace entirely without
	 * touching any other framework code — fog is independent of nav.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision",
		meta = (DisplayName = "Fog Of War Class", MetaClass = "/Script/SeinARTSFogOfWar.SeinFogOfWar"))
	FSoftClassPath FogOfWarClass;

	/**
	 * Default fog-of-war grid cell edge in world units (per-volume override
	 * supported on `ASeinFogOfWarVolume`). Independent of nav cell size — the
	 * fog grid is typically coarser than nav because vision doesn't need
	 * sub-meter granularity. Smaller values = crisper fog edges + higher
	 * memory; larger = cheaper stamps + chunkier edges.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision", meta = (ClampMin = "10.0", UIMin = "50", UIMax = "1600"))
	float VisionCellSize;

	/**
	 * Designer-configurable N-layers for the EVNNNNNN cell bitfield. Slot 0 →
	 * bit 2 (N0), slot 5 → bit 7 (N5). Exactly 6 slots, framework-enforced.
	 *
	 * The framework-default "Normal" layer is NOT in this array — it's reserved
	 * as the V bit (bit 1) and always present. These 6 slots are exclusively
	 * for additional channels a game needs beyond generic visibility: Stealth,
	 * Thermal, Radar, DetectorPerception, Acoustic, Infrared, etc. All 6 ship
	 * disabled — opt in by naming + enabling the slots your game uses.
	 *
	 * Layer semantics (DESIGN §12):
	 *  - Vision source perceives layer L iff its PerceptionLayers contains
	 *    L's name. "Normal" is always accepted and maps to V.
	 *  - Entity is emitted on layer L iff its EmissionLayers contains L's name.
	 *  - Entity E is visible to observer O iff ∃ source S owned by O such that
	 *    S's PerceptionLayers ∩ E's EmissionLayers ≠ ∅ AND E's cell has the
	 *    corresponding bit set in O's VisionGroup bitfield (V for Normal,
	 *    N(k-1) for designer slot k).
	 *
	 * Renaming a slot is safe. Reordering / inserting mid-array shifts every
	 * higher-slot bit → breaks replays + saves. Only append or rename.
	 */
	UPROPERTY(Config, EditAnywhere, EditFixedSize, Category = "Vision",
		meta = (TitleProperty = "LayerName"))
	TArray<FSeinVisionLayerDefinition> VisionLayers;

	/**
	 * Vision tick cadence in sim-ticks. `VisionTickInterval = N` means vision
	 * recomputes every N-th sim tick (e.g. N=3 at 30 Hz sim → 10 Hz fog).
	 * Higher values = cheaper (vision stamp math runs less often) but more
	 * perceptual latency on units entering/exiting vision. Below ~15 Hz the
	 * latency is imperceptible; above ~5 Hz it starts to feel stuttery. Must
	 * be deterministic across clients, so this is plugin-scoped — never
	 * per-machine.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision",
		meta = (ClampMin = "1", ClampMax = "60", UIMin = "1", UIMax = "10"))
	int32 VisionTickInterval;

	/**
	 * Render-side fog tick rate in Hz. Independent of `VisionTickInterval` —
	 * this governs only the debug-viz + UI readback cadence, not sim state.
	 * The sim-tick path is always deterministic; the render path runs on
	 * wall-clock and can lag freely.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "5.0", UIMax = "30.0"))
	float FogRenderTickRate;

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
