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

	// Locate the plugin's Resources directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SeinARTSFramework"));
	if (Plugin.IsValid())
	{
		StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("BrandKit"));
	}

	// Class icon (16x16) — shown in Content Browser list view
	StyleSet->Set(
		"ClassIcon.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTS_GrayClassIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	// Class thumbnail (64x64) — shown in Content Browser tile view
	StyleSet->Set(
		"ClassThumbnail.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTS_GrayClassIcon64"), TEXT(".png")), FVector2D(64.0f, 64.0f))
	);

	// Wordmark for dialog headers
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
