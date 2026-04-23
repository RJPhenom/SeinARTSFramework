/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionData.h
 * @brief   Per-entity vision emission + perception config (DESIGN.md §12).
 *          Ships as a sim component payload; designers author via UDS +
 *          USeinStructComponent (DESIGN §2).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "SeinVisionData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisionData
{
	GENERATED_BODY()

	/** Radius (world units) this entity sees. Feeds the circle stamp size in
	 *  the vision subsystem. At stamp time the radius is converted to cell
	 *  count by the active vision-grid cell size (plugin-settings
	 *  `VisionCellSize` or per-volume override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint VisionRange = FFixedPoint::FromInt(1000);

	/** If true, this entity contributes to its owning player's VisionGroup each
	 *  vision tick (registered as a source in the stamp-delta registry). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	bool bEmits = true;

	/** Require line-of-sight (occluded by vision blockers). MVP ignores
	 *  blockers and treats the entire radius as visible; clipped-raycast LOS
	 *  is deferred. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	bool bRequiresLOS = false;

	/** Layer names this entity *perceives* — which N-slot bits a stamp from
	 *  this emitter will set in the vision bitfield. Empty → defaults to
	 *  {"Normal"} at stamp time (so designers who ignore layers still get the
	 *  default layer semantics).
	 *
	 *  Names must match `FSeinVisionLayerDefinition.LayerName` entries in
	 *  Project Settings > Plugins > SeinARTS > Vision Layers. Names not found
	 *  in the plugin settings are ignored (with a dev-only warning on first
	 *  miss). Disabled slots are silently skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	TArray<FName> PerceptionLayers;

	/** Layer names this entity is *emitted on* — which N-slot bits a visibility
	 *  query will test against this entity. Empty → defaults to {"Normal"} at
	 *  query time. Stealth / detection: a camouflaged unit emits only on
	 *  {"Stealth"}; regular perceivers with {"Normal"} can't see it; detector
	 *  units with {"Normal", "Stealth"} can. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	TArray<FName> EmissionLayers;
};
