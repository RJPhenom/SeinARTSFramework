/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarTypes.h
 * @brief   Shared types exchanged between the fog-of-war subsystem and the
 *          active USeinFogOfWar impl. Kept header-only so both the base class
 *          and the default impl can consume the same structs without forcing
 *          a dep on the impl's header.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinPlayerID.h"

/** Debug-viz Z offset (world units) above the terrain-trace hit. Keeps fog
 *  cells clearly above the nav debug layer (which draws at bake cell height
 *  + 2cm) and well clear of z-buffer precision issues at typical camera
 *  ranges. Used by both the impl's CollectDebugCellQuads and the debug
 *  component's volume-rasterization fallback so the two paths stay coherent. */
#define SEIN_FOW_DEBUG_Z_OFFSET  50.0f

/** Bit 0 of the EVNNNNNN per-cell bitfield: sticky "has ever been visible". */
#define SEIN_FOW_BIT_EXPLORED  (uint8)(1u << 0)
/** Bit 1: framework-reserved "Normal" visibility — always available. */
#define SEIN_FOW_BIT_NORMAL    (uint8)(1u << 1)
/** Convenience: mask of all six designer-configurable custom layer bits. */
#define SEIN_FOW_MASK_CUSTOM   (uint8)(0xFCu)
/** Convenience: every visibility bit (Normal + Custom); Explored excluded. */
#define SEIN_FOW_MASK_VISIBLE  (uint8)(0xFEu)

/**
 * Per-source vision config handed to the fog-of-war impl when an entity
 * registers or its config changes. The impl is responsible for diffing
 * against prior state and applying refcount deltas.
 */
struct FSeinVisionSourceParams
{
	/** World position of the source (stamp origin). */
	FFixedVector WorldPos = FFixedVector::ZeroVector;

	/** Eye height for the lampshade test. */
	FFixedPoint EyeHeight = FFixedPoint::FromInt(180);

	/** Default (Normal / V bit) stamp radius. Zero disables the V-bit stamp. */
	FFixedPoint NormalRadius = FFixedPoint::FromInt(1000);

	/** Per-custom-layer stamp radii. Index N maps to EVNNNNNN bit (2 + N).
	 *  A zero entry disables that layer for this source regardless of
	 *  plugin-settings configuration. */
	FFixedPoint CustomRadii[6] = {
		FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::Zero,
		FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::Zero
	};

	/** Player whose VisionGroup the stamp writes into. */
	FSeinPlayerID OwnerPlayer;
};

/**
 * Per-blocker config for dynamic world-space vision blockers (smoke, etc.).
 * Static world blockers are baked into the asset and don't use this path.
 */
struct FSeinVisionBlockerParams
{
	FFixedVector WorldPos = FFixedVector::ZeroVector;
	FFixedPoint BlockerHeight = FFixedPoint::FromInt(300);
	/** Bits 1..7 of the cell bitfield this blocker occludes. */
	uint8 BlockedLayerMask = 0xFE;
};
