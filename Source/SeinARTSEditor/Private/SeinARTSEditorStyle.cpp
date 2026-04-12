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

void FSeinARTSEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(FName(TEXT("SeinARTSEditorStyle")));

	// Locate the plugin directories
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SeinARTSFramework"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString PluginBaseDir = Plugin->GetBaseDir();
	const FString BrandKitDir = PluginBaseDir / TEXT("Resources") / TEXT("BrandKit");
	const FString IconsDir = PluginBaseDir / TEXT("Source") / TEXT("SeinARTSEditor") / TEXT("Assets");

	StyleSet->SetContentRoot(BrandKitDir);

	// ==================== Unit (SeinActor) ====================

	StyleSet->Set(
		"ClassIcon.SeinActor",
		new FSlateImageBrush(IconsDir / TEXT("SeinUnitIcon16.png"), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinActor",
		new FSlateImageBrush(IconsDir / TEXT("SeinUnitIcon92.png"), FVector2D(92.0f, 92.0f))
	);

	// ==================== Ability (SeinAbility) ====================

	StyleSet->Set(
		"ClassIcon.SeinAbility",
		new FSlateImageBrush(IconsDir / TEXT("SeinAbilityIcon16.png"), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinAbility",
		new FSlateImageBrush(IconsDir / TEXT("SeinAbilityIcon92.png"), FVector2D(92.0f, 92.0f))
	);

	// ==================== Component (UserDefinedStruct) ====================

	// Registered for Slate style lookup; struct thumbnails use the renderer
	StyleSet->Set(
		"SeinARTS.ComponentIcon16",
		new FSlateImageBrush(IconsDir / TEXT("SeinComponentIcon16.png"), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"SeinARTS.ComponentIcon92",
		new FSlateImageBrush(IconsDir / TEXT("SeinComponentIcon92.png"), FVector2D(92.0f, 92.0f))
	);

	// ==================== Branding ====================

	StyleSet->Set(
		"SeinARTS.Wordmark",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTS_Wordmark_White_Small"), TEXT(".png")), FVector2D(165.0f, 52.0f))
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
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
