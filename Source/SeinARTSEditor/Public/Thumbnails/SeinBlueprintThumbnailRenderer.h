/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinBlueprintThumbnailRenderer.h
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer that adds a colored identity bar
 *				to SeinARTS Blueprint thumbnails (Unit, Ability).
 */

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/BlueprintThumbnailRenderer.h"
#include "SeinBlueprintThumbnailRenderer.generated.h"

/**
 * Extends the default Blueprint thumbnail renderer to draw a colored
 * bar at the bottom of SeinARTS-derived Blueprints:
 *
 *   Unit (ASeinActor parent)   — Blue   (#0095FF)
 *   Ability (USeinAbility parent) — Red (#FF0000)
 *
 * Non-SeinARTS Blueprints render with the standard thumbnail (no bar).
 */
UCLASS()
class USeinBlueprintThumbnailRenderer : public UBlueprintThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;

private:
	/** Returns the color bar color for a given Blueprint, or transparent if not a SeinARTS type. */
	static FLinearColor GetBarColor(UObject* Object);
};
