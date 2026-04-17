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
#include "Widgets/SeinWidgetBlueprint.h"
#include "Components/ActorComponents/SeinComponentBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

bool USeinBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const ESeinAssetType Type = ClassifyBlueprint(Object);
	const bool bSuperResult = (Type == ESeinAssetType::None) ? Super::CanVisualizeAsset(Object) : false;
	const bool bResult = (Type != ESeinAssetType::None) || bSuperResult;

	UE_LOG(LogTemp, Log, TEXT("[SeinARTSEditor] CanVisualizeAsset for %s (class=%s) classified=%d -> %s"),
		Object ? *Object->GetName() : TEXT("<null>"),
		Object && Object->GetClass() ? *Object->GetClass()->GetName() : TEXT("<null>"),
		(int32)Type,
		bResult ? TEXT("true") : TEXT("false"));

	return bResult;
}

void USeinBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const ESeinAssetType AssetType = ClassifyBlueprint(Object);

	UE_LOG(LogTemp, Log, TEXT("[SeinARTSEditor] USeinBlueprintThumbnailRenderer::Draw for %s — classified as %d"),
		Object ? *Object->GetName() : TEXT("<null>"), (int32)AssetType);

	// Non-SeinARTS Blueprints: fall through to default rendering
	if (AssetType == ESeinAssetType::None)
	{
		Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	// ---- Dark background ----
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

	// ---- Icon (from Slate style set) ----
	const FTexture* IconResource = GetIconResource(AssetType);
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

	// Color bar is handled by the Content Browser's SAssetMenuIcon via IAssetTypeActions::GetTypeColor()
}

const FTexture* USeinBlueprintThumbnailRenderer::GetIconResource(ESeinAssetType Type)
{
	FName TextureName;
	switch (Type)
	{
	case ESeinAssetType::Unit:      TextureName = FName(TEXT("SeinUnitIcon92"));      break;
	case ESeinAssetType::Ability:   TextureName = FName(TEXT("SeinAbilityIcon92"));   break;
	case ESeinAssetType::Component: TextureName = FName(TEXT("SeinComponentIcon92")); break;
	case ESeinAssetType::Widget:    TextureName = FName(TEXT("SeinWidgetIcon92"));    break;
	default: return nullptr;
	}

	UTexture2D* Texture = FSeinARTSEditorStyle::GetIconTexture(TextureName);
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

	// Widget and Component are checked by asset class (USeinWidgetBlueprint /
	// USeinComponentBlueprint), not parent class, because those factories produce
	// dedicated UCLASSes. Unit/Ability use parent class checks since they're
	// plain UBlueprint assets differentiated only by their parent.
	if (Blueprint->IsA<USeinWidgetBlueprint>())
	{
		return ESeinAssetType::Widget;
	}

	if (Blueprint->IsA<USeinComponentBlueprint>())
	{
		return ESeinAssetType::Component;
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
	case ESeinAssetType::Unit:      return FLinearColor(0.0f, 0.584f, 1.0f, 1.0f);    // #0095FF
	case ESeinAssetType::Ability:   return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);      // #FF0000
	case ESeinAssetType::Component: return FLinearColor(1.0f, 0.584f, 0.0f, 1.0f);    // #FF9500
	case ESeinAssetType::Widget:    return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("8844AA"))); // #8844AA
	default:                        return FLinearColor::Transparent;
	}
}
