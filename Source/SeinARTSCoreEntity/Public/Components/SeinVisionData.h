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

	/** Radius (world units) this entity sees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	FFixedPoint SightRadius = FFixedPoint::FromInt(1000);

	/** If true, this entity contributes to its owning player's VisionGroup each vision tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	bool bEmits = true;

	/** Require line-of-sight (occluded by FSeinVisionBlockerData). MVP ignores blockers
	 *  and treats the entire radius as visible; proper clipped raycasts land in a polish pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	bool bRequiresLOS = false;

	/** Perception layer bitmask — which custom emission-layer bits this entity perceives.
	 *  Bit 0 = default layer; designers extend via VisionLayers plugin setting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	uint8 PerceptionLayerMask = 0x01;

	/** Emission layer bitmask — which custom layers this entity is emitted on (stealth/detection). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	uint8 EmissionLayerMask = 0x01;
};
