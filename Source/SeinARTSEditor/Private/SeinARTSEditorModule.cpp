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
#include "SeinAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Thumbnails/SeinBlueprintThumbnailRenderer.h"
#include "Thumbnails/SeinComponentThumbnailRenderer.h"
#include "Engine/Blueprint.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Actor/SeinActorBlueprint.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "EdGraphUtilities.h"
#include "Graph/SeinPinFactory.h"

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

	// Register asset type actions (per-type colors for content browser)
	RegisterAssetTypeActions();

	// Register custom thumbnail renderers for our Blueprint subclasses.
	// These are unique classes so no unregister needed — no conflict with engine defaults.
	UThumbnailManager& ThumbnailMgr = UThumbnailManager::Get();
	ThumbnailMgr.RegisterCustomRenderer(
		USeinActorBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	ThumbnailMgr.RegisterCustomRenderer(
		USeinAbilityBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	ThumbnailMgr.UnregisterCustomRenderer(UUserDefinedStruct::StaticClass());
	ThumbnailMgr.RegisterCustomRenderer(
		UUserDefinedStruct::StaticClass(),
		USeinComponentThumbnailRenderer::StaticClass()
	);

	// Register custom pin color factory for fixed-point structs
	SeinPinFactory = MakeShared<FSeinPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(SeinPinFactory);
}

void FSeinARTSEditorModule::ShutdownModule()
{
	if (SeinPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(SeinPinFactory);
		SeinPinFactory.Reset();
	}
	UnregisterAssetTypeActions();
	FSeinARTSEditorStyle::Shutdown();
}

EAssetTypeCategories::Type FSeinARTSEditorModule::GetAssetCategoryBit()
{
	return SeinARTSCategoryBit;
}

void FSeinARTSEditorModule::RegisterAssetTypeActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	auto RegisterAction = [&](const TSharedRef<IAssetTypeActions>& Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredActions.Add(Action);
	};

	RegisterAction(MakeShared<FAssetTypeActions_SeinActorBlueprint>());
	RegisterAction(MakeShared<FAssetTypeActions_SeinAbilityBlueprint>());
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
