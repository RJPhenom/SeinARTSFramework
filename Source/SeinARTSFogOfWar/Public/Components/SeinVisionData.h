/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionData.h
 * @brief   Per-entity vision emission config. Carried into sim storage by
 *          USeinVisionComponent. Designers configure a default stamp radius
 *          (the "Normal" layer / V bit) plus up to six opt-in per-layer
 *          radii matching the project's vision-layer slots in plugin settings.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "SeinVisionData.generated.h"

/**
 * One designer-configurable layer slot on an entity's vision component.
 *
 * Slots parallel the 6 N-slots in `USeinARTSCoreSettings::VisionLayers`: slot
 * index N (0..5) maps to bit (2 + N) of the EVNNNNNN cell bitfield. The
 * details customization pulls the slot's display name from the plugin
 * settings row at the same index — the component itself stores only the
 * enable flag and radius, indexed by position.
 */
USTRUCT(BlueprintType)
struct SEINARTSFOGOFWAR_API FSeinVisionLayerRadius
{
	GENERATED_BODY()

	/** If false, this source does NOT stamp the matching N-bit, even if the
	 *  layer is enabled in plugin settings. Lets designers author a unit that
	 *  sees on "Normal" only without filling in every custom slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	bool bEnabled = false;

	/** Stamp radius (world units) for this layer. Independent of the default
	 *  `VisionRadius` on the parent component — a unit can have a tight Normal
	 *  radius and a wide Thermal radius, for example. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision",
		meta = (EditCondition = "bEnabled"))
	FFixedPoint Radius = FFixedPoint::FromInt(500);
};

/**
 * Per-entity vision-source payload. Lives in sim storage as a reflected
 * component; authored on actor Blueprints via USeinVisionComponent.
 *
 * Shadowcast/lampshade model: the subsystem stamps this source's EVNNNNNN
 * bits into its owner's VisionGroup grid, using `VisionRadius` for the V bit
 * and each enabled `LayerSlots[N]`'s radius for the N-th custom layer.
 * `EyeHeight` participates in the shadowcast blocker test — tall units see
 * over short blockers, short units don't.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSFOGOFWAR_API FSeinVisionData
{
	GENERATED_BODY()

	FSeinVisionData()
	{
		LayerSlots.SetNum(6);
	}

	/** Default (Normal / V bit) stamp radius in world units. Always stamped
	 *  when this entity is a live vision source. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint VisionRadius = FFixedPoint::FromInt(1000);

	/** Eye height (world units) for the shadowcast lampshade test. Typical
	 *  values: infantry ~180, vehicles ~250, aircraft high enough that nothing
	 *  blocks them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint EyeHeight = FFixedPoint::FromInt(180);

	/** Exactly 6 per-layer stamp slots, matching the project's configured
	 *  vision layers (`USeinARTSCoreSettings::VisionLayers`). Slot N → bit
	 *  (2 + N) of the cell bitfield. Detail panel customization labels each
	 *  row with the layer's configured name; unnamed/disabled settings slots
	 *  still appear in the array but have no effect at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision",
		EditFixedSize)
	TArray<FSeinVisionLayerRadius> LayerSlots;
};
