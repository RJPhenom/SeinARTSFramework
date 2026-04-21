/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionBlockerData.h
 * @brief   Entity-level vision blocker (DESIGN.md §12). MVP ships the struct
 *          only; clipped-raycast LOS occlusion lands in a polish pass.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinVisionBlockerData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisionBlockerData
{
	GENERATED_BODY()

	/** Layer bitmask this blocker occludes. 0xFF = blocks all layers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	uint8 BlockedLayers = 0xFF;

	/** Layer index this blocker lives on (for multi-layer nav). Defaults to ground layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Vision")
	uint8 Layer = 0;
};
