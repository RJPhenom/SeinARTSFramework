/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinBlueprintThumbnailRenderer.cpp
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer implementation.
 */

#include "Thumbnails/SeinBlueprintThumbnailRenderer.h"
#include "Actor/SeinActor.h"
#include "Abilities/SeinAbility.h"
#include "Engine/Blueprint.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

void USeinBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	// Let the base class render the standard blueprint thumbnail (icon on dark bg)
	Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);

	// Draw the color identity bar at the bottom for SeinARTS types
	const FLinearColor BarColor = GetBarColor(Object);
	if (BarColor.A > 0.0f)
	{
		const float BarHeight = FMath::Max(4.0f, Height * 0.06f);
		const float BarY = Y + Height - BarHeight;

		FCanvasTileItem BarItem(
			FVector2D(X, BarY),
			GWhiteTexture,
			FVector2D(Width, BarHeight),
			BarColor
		);
		BarItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BarItem);
	}
}

FLinearColor USeinBlueprintThumbnailRenderer::GetBarColor(UObject* Object)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint || !Blueprint->ParentClass)
	{
		return FLinearColor::Transparent;
	}

	// Unit — Blue #0095FF
	if (Blueprint->ParentClass->IsChildOf(ASeinActor::StaticClass()))
	{
		return FLinearColor(0.0f, 0.584f, 1.0f, 1.0f); // #0095FF
	}

	// Ability — Red #FF0000
	if (Blueprint->ParentClass->IsChildOf(USeinAbility::StaticClass()))
	{
		return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f); // #FF0000
	}

	return FLinearColor::Transparent;
}
