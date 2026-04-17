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
#include "Engine/Blueprint.h"
#include "Actor/SeinActorBlueprint.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Components/ActorComponents/SeinComponentBlueprint.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "KismetCompiler.h"
#include "BlueprintCompilationManager.h"
#include "EdGraphUtilities.h"
#include "Graph/SeinPinFactory.h"
#include "PropertyEditorModule.h"
#include "Details/SeinFixedPointDetails.h"
#include "Details/SeinInstancedStructDetails.h"
#include "Compile/SeinComponentCompilerExtension.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

EAssetTypeCategories::Type FSeinARTSEditorModule::SeinARTSCategoryBit = EAssetTypeCategories::Misc;

void FSeinARTSEditorModule::StartupModule()
{
	FSeinARTSEditorStyle::Initialize();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	if (SeinARTSCategoryBit == EAssetTypeCategories::Misc)
	{
		SeinARTSCategoryBit = AssetTools.RegisterAdvancedAssetCategory(
			FName(TEXT("SeinARTS")),
			LOCTEXT("SeinARTSAssetCategory", "SeinARTS")
		);
	}

	RegisterAssetTypeActions();

	// UMG's widget compiler is registered per-class via a direct map lookup
	// (no hierarchy walk) — see FKismetCompilerContext::GetCompilerForBP.
	// We must register USeinWidgetBlueprint explicitly, otherwise the lookup
	// misses, falls back to the generic FKismetCompilerContext, and the
	// WidgetTree never gets copied into the generated class (AddToViewport
	// would render nothing despite compilation succeeding).
	FKismetCompilerContext::RegisterCompilerForBP(
		USeinWidgetBlueprint::StaticClass(),
		&UWidgetBlueprint::GetCompilerForWidgetBP
	);

	// Register custom thumbnail renderers for our Blueprint subclasses.
	UThumbnailManager& ThumbnailMgr = UThumbnailManager::Get();
	ThumbnailMgr.RegisterCustomRenderer(
		USeinActorBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	ThumbnailMgr.RegisterCustomRenderer(
		USeinAbilityBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	ThumbnailMgr.RegisterCustomRenderer(
		USeinComponentBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	// Intentionally do NOT register our custom renderer for USeinWidgetBlueprint.
	// Widget BPs need the engine's UWidgetBlueprintThumbnailRenderer to draw the
	// designer preview; our 92px icon would otherwise overwrite it. The 16px
	// corner badge and the purple color bar are still applied via
	// ClassIcon.SeinWidgetBlueprint / ClassIcon.SeinUserWidget (style set) and
	// UAssetDefinition_SeinWidgetBlueprint::GetAssetColor (asset definition) —
	// both decorate the tile independently of the thumbnail renderer.

	SeinPinFactory = MakeShared<FSeinPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(SeinPinFactory);

	// Register the BP compile hook that synthesises a sidecar UUserDefinedStruct
	// for Blueprint subclasses of USeinDynamicComponent. TStrongObjectPtr
	// keeps the object alive across the editor session and tears down safely.
	ComponentCompilerExtension.Reset(NewObject<USeinComponentCompilerExtension>(GetTransientPackage(), NAME_None, RF_Transient));
	FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), ComponentCompilerExtension.Get());

	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("FixedPoint"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinFixedPointDetails::MakeInstance));

		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("InstancedStruct"));
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("InstancedStruct"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinInstancedStructDetails::MakeInstance));

		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FSeinARTSEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("FixedPoint"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("InstancedStruct"));
	}

	if (SeinPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(SeinPinFactory);
		SeinPinFactory.Reset();
	}

	// The compilation manager still holds a strong ref to the extension via its
	// internal TObjectPtr map; we just drop our side. TStrongObjectPtr's
	// destructor handles the "UObject array is mid-teardown" case gracefully.
	ComponentCompilerExtension.Reset();

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
	RegisterAction(MakeShared<FAssetTypeActions_SeinComponentBlueprint>());
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
