/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinBlueprintThumbnailRenderer.cpp
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer implementation.
 */

#include "Thumbnails/SeinBlueprintThumbnailRenderer.h"
#include "SeinARTSEditorStyle.h"
#include "Actor/SeinActor.h"
#include "Abilities/SeinAbility.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

void USeinBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const ESeinAssetType AssetType = ClassifyBlueprint(Object);

	// Non-SeinARTS Blueprints: fall through to default rendering
	if (AssetType == ESeinAssetType::None)
	{
		Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	const float BarHeight = FMath::Max(6.0f, Height * 0.08f);

	// ---- Dark background ----
	{
		FCanvasTileItem BgItem(
			FVector2D(X, Y),
			GWhiteTexture,
			FVector2D(Width, Height),
			FLinearColor(0.08f, 0.08f, 0.08f, 1.0f)
		);
		BgItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BgItem);
	}

	// ---- Icon (from Slate style set) ----
	const FTexture* IconResource = GetIconResource(AssetType);
	if (IconResource)
	{
		const float IconPadding = Width * 0.1f;
		const float AvailableHeight = Height - BarHeight - IconPadding;
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

	// ---- Color bar at bottom ----
	{
		const FLinearColor BarColor = GetBarColor(AssetType);
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

const FTexture* USeinBlueprintThumbnailRenderer::GetIconResource(ESeinAssetType Type)
{
	const FName BrushName = (Type == ESeinAssetType::Unit)
		? FName(TEXT("ClassThumbnail.SeinActor"))
		: FName(TEXT("ClassThumbnail.SeinAbility"));

	const FSlateBrush* Brush = FSeinARTSEditorStyle::Get().GetBrush(BrushName);
	if (!Brush || Brush == FCoreStyle::Get().GetDefaultBrush())
	{
		return nullptr;
	}

	UObject* ResourceObject = Brush->GetResourceObject();
	if (!ResourceObject)
	{
		return nullptr;
	}

	UTexture2D* Texture = Cast<UTexture2D>(ResourceObject);
	if (!Texture)
	{
		return nullptr;
	}

	return Texture->GetResource();
}

USeinBlueprintThumbnailRenderer::ESeinAssetType USeinBlueprintThumbnailRenderer::ClassifyBlueprint(UObject* Object)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint || !Blueprint->ParentClass)
	{
		return ESeinAssetType::None;
	}

	if (Blueprint->ParentClass->IsChildOf(ASeinActor::StaticClass()))
	{
		return ESeinAssetType::Unit;
	}

	if (Blueprint->ParentClass->IsChildOf(USeinAbility::StaticClass()))
	{
		return ESeinAssetType::Ability;
	}

	return ESeinAssetType::None;
}

FLinearColor USeinBlueprintThumbnailRenderer::GetBarColor(ESeinAssetType Type)
{
	switch (Type)
	{
	case ESeinAssetType::Unit:    return FLinearColor(0.0f, 0.584f, 1.0f, 1.0f);  // #0095FF
	case ESeinAssetType::Ability: return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);    // #FF0000
	default:                      return FLinearColor::Transparent;
	}
}
