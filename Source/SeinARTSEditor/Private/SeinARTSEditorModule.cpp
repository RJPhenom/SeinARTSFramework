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
#include "Thumbnails/SeinStructThumbnailRenderer.h"
#include "Validators/SeinDeterministicStructValidator.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/Blueprint.h"
#include "Actor/SeinActorBlueprint.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Effects/SeinEffectBlueprint.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "KismetCompiler.h"
#include "EdGraphUtilities.h"
#include "Graph/SeinPinFactory.h"
#include "PropertyEditorModule.h"
#include "Details/SeinFixedPointDetails.h"
#include "Details/SeinInstancedStructDetails.h"
// Volume details panels live in their owning system modules (SeinARTSNavigation
// + SeinARTSFogOfWar), not here — preserves the "each subsystem self-contained"
// pattern. Each module registers its own customization at StartupModule under
// `#if WITH_EDITOR`, so the editor module never imports their headers.
#include "Details/SeinVisionDataDetails.h"
#include "Details/SeinVisionStampDetails.h"
#include "Details/SeinExtentsDataDetails.h"
#include "Details/SeinMovementDataDetails.h"
#include "Visualizers/SeinExtentsComponentVisualizer.h"
#include "Components/ActorComponents/SeinExtentsComponent.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Brokers/SeinStructAssetBroker.h"
#include "ComponentAssetBroker.h"
#include "Components/ActorComponents/SeinStructComponent.h"

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
		USeinEffectBlueprint::StaticClass(),
		USeinBlueprintThumbnailRenderer::StaticClass()
	);
	// Sein UDSes (Right-click → Component) get the SeinComponentIcon92 tile.
	// The renderer falls through to the engine default for non-Sein UDSes.
	ThumbnailMgr.RegisterCustomRenderer(
		UUserDefinedStruct::StaticClass(),
		USeinStructThumbnailRenderer::StaticClass()
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

	// Wire UDS → USeinStructComponent composition per DESIGN.md §2.
	// Designers drag a Sein-marked UDS onto an actor BP's components panel;
	// the broker creates a USeinStructComponent and types its Data by the UDS.
	StructAssetBroker = MakeShared<FSeinStructAssetBroker>();
	FComponentAssetBrokerage::RegisterBroker(StructAssetBroker, USeinStructComponent::StaticClass(), /*bSetAsPrimary=*/true, /*bMapComponentForAssets=*/true);

	// Enforce the determinism whitelist on Sein UDSes. The validator auto-
	// registers with FStructureEditorUtils::FStructEditorManager in its ctor
	// (via INotifyOnStructChanged/InnerListenerType) and auto-removes in its
	// dtor when UDSValidator is reset at shutdown.
	UDSValidator = MakeUnique<FSeinDeterministicStructValidator>();

	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("FixedPoint"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinFixedPointDetails::MakeInstance));

		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("InstancedStruct"));
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("InstancedStruct"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinInstancedStructDetails::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("SeinVisionData"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinVisionDataDetails::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("SeinVisionStamp"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinVisionStampDetails::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("SeinExtentsData"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinExtentsDataDetails::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("SeinMovementData"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinMovementDataDetails::MakeInstance));

		// Volume details panels (ASeinNavVolume, ASeinFogOfWarVolume) are
		// registered by their owning system modules' StartupModule under
		// `#if WITH_EDITOR` — keeps the editor module decoupled from those
		// systems. Replace the nav class via plugin settings; the framework's
		// bake button stays on the volume regardless.

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	// Component visualizers — drawn in the BP/level editor viewport when the
	// owning component is selected. Lets designers eyeball Extents shapes
	// without opening PIE.
	if (GUnrealEd != nullptr)
	{
		TSharedPtr<FComponentVisualizer> ExtentsViz = MakeShared<FSeinExtentsComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(USeinExtentsComponent::StaticClass()->GetFName(), ExtentsViz);
		ExtentsViz->OnRegister();
	}
}

void FSeinARTSEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("FixedPoint"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("InstancedStruct"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("SeinVisionData"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("SeinVisionStamp"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("SeinExtentsData"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("SeinMovementData"));
		// Volume class-layouts unregistered by their owning system modules.
	}

	if (GUnrealEd != nullptr)
	{
		GUnrealEd->UnregisterComponentVisualizer(USeinExtentsComponent::StaticClass()->GetFName());
	}

	if (SeinPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(SeinPinFactory);
		SeinPinFactory.Reset();
	}

	if (StructAssetBroker.IsValid())
	{
		FComponentAssetBrokerage::UnregisterBroker(StructAssetBroker);
		StructAssetBroker.Reset();
	}

	// Dtor unregisters the listener via INotifyOnStructChanged RAII.
	UDSValidator.Reset();

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
	RegisterAction(MakeShared<FAssetTypeActions_SeinEffectBlueprint>());
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
