/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinComponentThumbnailRenderer.h
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer for SeinARTS Component structs.
 *				Draws the component icon with an orange identity bar.
 *				Non-SeinARTS UserDefinedStructs fall through to default rendering.
 */

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "SeinComponentThumbnailRenderer.generated.h"

/**
 * Renders custom thumbnails for SeinARTS Component structs.
 * Identified by the "SeinARTSComponent" metadata tag set at creation time.
 *
 *   Component — Component icon + Orange bar (#FF9500)
 */
UCLASS()
class USeinComponentThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	virtual bool CanVisualizeAsset(UObject* Object) override;

private:
	static bool IsSeinComponent(UObject* Object);
};
