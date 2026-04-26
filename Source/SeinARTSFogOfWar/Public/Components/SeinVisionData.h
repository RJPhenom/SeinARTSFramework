/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionData.h
 * @brief   Per-entity vision-source payload. One or more stamp shapes
 *          (radial / rect / cone), each tagged with the EVNNNNNN layer bits
 *          it emits visibility on. A standard infantry unit has one radial
 *          stamp on the V (Normal) bit; a building with garrison cones
 *          authors one stamp per window, each on V, with bEnabled flipped
 *          by the garrison system; a unit with thermal sight adds another
 *          stamp on the N-th custom layer.
 */

#pragma once

#include "CoreMinimal.h"
#include "Stamping/SeinStampShape.h"
#include "Types/FixedPoint.h"
#include "SeinFogOfWarTypes.h"
#include "SeinVisionData.generated.h"

/**
 * One vision stamp on a source. Pairs an FSeinStampShape (geometry + pose +
 * bEnabled) with the EVNNNNNN bits this stamp emits on. Multiple stamps live
 * on FSeinVisionData::Stamps; each one runs its own shadowcast pass against
 * the cells it covers.
 *
 * LayerMask examples:
 *   0x02 → Visible (V / Normal sight) — the standard case.
 *   0x04 → N0 (e.g. Thermal) — a separate stamp for thermal emission.
 *   0x06 → V | N0 — same shape stamps both layers (rare; use one stamp
 *          per layer when radii or shapes should differ).
 *
 * Bit 0 (Explored) is set automatically by every stamp pass — it never
 * needs to be in the mask.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSFOGOFWAR_API FSeinVisionStamp
{
	GENERATED_BODY()

	/** Stamp geometry — shape, local offset, yaw offset, per-shape params,
	 *  bEnabled flag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Vision",
		meta = (ShowOnlyInnerProperties))
	FSeinStampShape Shape;

	/** Which EVNNNNNN bits this stamp emits visibility on. Bit 1 = V
	 *  (Normal), bits 2..7 = N0..N5 custom layers (named in plugin
	 *  settings). Default 0x02 = standard sight. Bit 0 (Explored) is
	 *  always set by every stamp; don't include it here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Vision",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit"))
	uint8 LayerMask = 0x02; // V bit (Normal)
};

/**
 * Per-entity vision-source payload. Lives in sim storage as a reflected
 * component; authored on actor Blueprints via USeinVisionComponent.
 *
 * Shadowcast / lampshade model: each enabled FSeinVisionStamp runs its
 * own per-bit pass over the cells it covers. Source eye Z =
 * `WorldPos.Z + EyeHeight` (tall units see over short blockers, short
 * units don't). For shaped stamps (cones at building windows, etc.),
 * the LOS source is the stamp's apex — `EntityPos + LocalOffset`
 * rotated by entity yaw — so a window-cone casts from the window, not
 * the building center.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSFOGOFWAR_API FSeinVisionData
{
	GENERATED_BODY()

	/** Eye height (world units) above the entity's sim Z, used by the
	 *  shadowcast lampshade test. Typical values: infantry ~180,
	 *  vehicles ~250, aircraft high enough that nothing blocks them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint EyeHeight = FFixedPoint::FromInt(180);

	/** Layer bitmask this entity emits ON — what other units' vision
	 *  queries match against THIS unit. Bit 1 = V (visible on Normal).
	 *  Camouflaged units flip to a Stealth-only bit; detected-always
	 *  units OR every layer.
	 *
	 *  This is the EMISSION-AS-TARGET side. The PERCEPTION-AS-OBSERVER
	 *  side is driven by the Stamps array below — what layer bits this
	 *  unit STAMPS into the cell grid as a vision source. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit"))
	uint8 EmissionLayerMask = 0x02; // V bit (Normal)

	/** Vision stamps emitted by this source. One radial stamp on the V bit
	 *  is the standard case (omnidirectional sight). Buildings with garrison
	 *  windows author one cone per window with `bEnabled = false`; the
	 *  garrison system flips them on per-occupied-slot. Thermal / stealth-
	 *  detector units add additional stamps on custom layer bits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	TArray<FSeinVisionStamp> Stamps;
};
