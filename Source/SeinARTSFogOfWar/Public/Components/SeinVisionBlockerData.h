/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionBlockerData.h
 * @brief   Per-entity dynamic vision blocker config. Static world blockers
 *          (walls, buildings, hedgerows) bake into the fog grid at level
 *          load; this struct is for *dynamic* blockers (smoke grenades,
 *          destructibles mid-animation) that register + unregister at
 *          runtime.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "SeinVisionBlockerData.generated.h"

/**
 * Dynamic vision-blocker payload. Carried into sim storage by
 * USeinVisionBlockerComponent.
 *
 * Lampshade model: the subsystem OR's this blocker's height into the
 * runtime blocker grid at the blocker's cell, then clears it on unregister
 * or height change. Sources whose stamp radius overlaps the changed cells
 * are flagged for re-stamp next tick.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSFOGOFWAR_API FSeinVisionBlockerData
{
	GENERATED_BODY()

	/** Blocker height (world units). Shadowcast compares against source
	 *  `EyeHeight + GroundHeight` — blockers shorter than the source's eye
	 *  don't occlude. Typical values: smoke ~400, low wall ~150, building
	 *  ~800. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint BlockerHeight = FFixedPoint::FromInt(300);

	/** Which EVNNNNNN bits this blocker occludes. Bit 1 = V (Normal), bits
	 *  2..7 = N0..N5. Bit 0 (Explored) is never blocked (history is sticky).
	 *  Default 0xFE = blocks every layer.
	 *
	 *  Per-layer blocker semantics: thermal vision may ignore smoke (clear
	 *  bits 2+ on the smoke blocker) while normal vision is occluded (keep
	 *  bit 1 set). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision",
		meta = (Bitmask))
	uint8 BlockedLayerMask = 0xFE;

	/** Disc radius (world units) for area blockers like smoke grenades.
	 *  Zero → single-cell blocker (destructibles, debris). Non-zero →
	 *  stamp every cell within Radius of the entity's position with this
	 *  blocker's Height + LayerMask. Deterministic disc stamp, same math
	 *  shape as the vision-source stamp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint Radius = FFixedPoint::Zero;
};
