/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinComponentThumbnailRenderer.cpp
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer for SeinARTS Component structs.
 */

#include "Thumbnails/SeinComponentThumbnailRenderer.h"
#include "SeinARTSEditorStyle.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

void USeinComponentThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (!IsSeinComponent(Object))
	{
		return;
	}

	// ---- Dark background (matches Content Browser's Recessed color #1A1A1A) ----
	{
		FCanvasTileItem BgItem(
			FVector2D(X, Y),
			GWhiteTexture,
			FVector2D(Width, Height),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("1A1A1A")))
		);
		BgItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BgItem);
	}

	// ---- Icon ----
	UTexture2D* Texture = FSeinARTSEditorStyle::GetIconTexture(FName(TEXT("SeinComponentIcon92")));
	if (Texture)
	{
		const FTexture* IconResource = Texture->GetResource();
		if (IconResource)
		{
			const float IconPadding = Width * 0.1f;
			const float AvailableHeight = Height - IconPadding;
			const float IconSize = FMath::Min((float)Width - IconPadding * 2.0f, AvailableHeight);
			const float IconX = X + (Width - IconSize) * 0.5f;
			const float IconY = Y + (AvailableHeight - IconSize) * 0.5f + IconPadding * 0.25f;

			FCanvasTileItem IconItem(
				FVector2D(IconX, IconY),
				IconResource,
				FVector2D(IconSize, IconSize),
				FLinearColor::White
			);
			IconItem.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(IconItem);
		}
	}

	// Color bar is handled by the Content Browser's SAssetMenuIcon via IAssetTypeActions::GetTypeColor()
}

bool USeinComponentThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	return IsSeinComponent(Object);
}

bool USeinComponentThumbnailRenderer::IsSeinComponent(UObject* Object)
{
	const UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Object);
	if (!Struct)
	{
		return false;
	}

	return Struct->HasMetaData(TEXT("SeinARTSComponent"));
}
