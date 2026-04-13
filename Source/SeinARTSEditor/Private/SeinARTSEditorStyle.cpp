/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorStyle.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the SeinARTS editor Slate style set.
 */

#include "SeinARTSEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FSeinARTSEditorStyle::StyleSet = nullptr;
FString FSeinARTSEditorStyle::IconsDir;

void FSeinARTSEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(FName(TEXT("SeinARTSEditorStyle")));

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SeinARTSFramework"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString BrandKitDir = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("BrandKit");
	IconsDir = BrandKitDir;
	StyleSet->SetContentRoot(BrandKitDir);

	// ==================== Unit (SeinActor) ====================

	StyleSet->Set(
		"ClassIcon.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinUnitIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinUnitIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Ability (SeinAbility) ====================

	StyleSet->Set(
		"ClassIcon.SeinAbility",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAbilityIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinAbility",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAbilityIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Component ====================

	StyleSet->Set(
		"ClassIcon.SeinComponent",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinComponent",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Branding ====================

	StyleSet->Set(
		"SeinARTS.Wordmark",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTSWordmark"), TEXT(".png")), FVector2D(164.0f, 48.0f))
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

FString FSeinARTSEditorStyle::GetIconPath(const FString& IconFilename)
{
	return IconsDir / IconFilename;
}

void FSeinARTSEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

const FSlateStyleSet& FSeinARTSEditorStyle::Get()
{
	check(StyleSet.IsValid());
	return *StyleSet;
}
