/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorModule.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the SeinARTS editor module.
 */

#include "SeinARTSEditorModule.h"
#include "SeinARTSEditorStyle.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Thumbnails/SeinBlueprintThumbnailRenderer.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

EAssetTypeCategories::Type FSeinARTSEditorModule::SeinARTSCategoryBit = EAssetTypeCategories::Misc;

void FSeinARTSEditorModule::StartupModule()
{
	FSeinARTSEditorStyle::Initialize();

	// Register custom asset category
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	if (SeinARTSCategoryBit == EAssetTypeCategories::Misc)
	{
		SeinARTSCategoryBit = AssetTools.RegisterAdvancedAssetCategory(
			FName(TEXT("SeinARTS")),
			LOCTEXT("SeinARTSAssetCategory", "SeinARTS")
		);
	}

	// Register custom thumbnail renderer for SeinARTS Blueprints (color bars)
	UThumbnailManager::Get().RegisterCustomRenderer(
		UBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
}

void FSeinARTSEditorModule::ShutdownModule()
{
	UnregisterAssetTypeActions();
	FSeinARTSEditorStyle::Shutdown();
}

EAssetTypeCategories::Type FSeinARTSEditorModule::GetAssetCategoryBit()
{
	return SeinARTSCategoryBit;
}

void FSeinARTSEditorModule::RegisterAssetTypeActions()
{
	// Reserved for future asset type actions
}

void FSeinARTSEditorModule::UnregisterAssetTypeActions()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredActions.Empty();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSeinARTSEditorModule, SeinARTSEditor)
