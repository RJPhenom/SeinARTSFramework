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
#include "ImageUtils.h"

TSharedPtr<FSlateStyleSet> FSeinARTSEditorStyle::StyleSet = nullptr;
FString FSeinARTSEditorStyle::IconsDir;
TMap<FName, TObjectPtr<UTexture2D>> FSeinARTSEditorStyle::IconTextures;

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

	// Per §2, "components" split into two tracks: (a) the UDS factory's
	// authoring thumbnail (`SeinSimComponent` — used by USeinSimComponentFactory's
	// GetNewAssetThumbnailOverride), and (b) the base AC class icon
	// (`SeinActorComponent` — UE walks the parent chain for the corner badge,
	// so this catches typed wrappers AND USeinStructComponent instances).
	auto RegisterComponentIcon = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
		);
	};
	auto RegisterComponentThumb = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
		);
	};

	RegisterComponentIcon(TEXT("ClassIcon.SeinSimComponent"));
	RegisterComponentIcon(TEXT("ClassIcon.SeinActorComponent"));
	// Direct UActorComponent subclasses in the SeinARTS ClassGroup — UE
	// looks up icons by exact class name, so these need their own entries
	// (the SeinActorComponent parent-chain lookup doesn't catch them).
	RegisterComponentIcon(TEXT("ClassIcon.SeinActorBridge"));
	RegisterComponentIcon(TEXT("ClassIcon.SeinArchetypeDefinition"));
	RegisterComponentIcon(TEXT("ClassIcon.SeinSelectionComponent"));

	RegisterComponentThumb(TEXT("ClassThumbnail.SeinSimComponent"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinActorComponent"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinActorBridge"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinArchetypeDefinition"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinSelectionComponent"));

	// ==================== Widget ====================

	// Same multi-key pattern as Component — the content browser corner badge
	// resolves via the BPGC's parent class chain (USeinUserWidget), not the
	// asset class, so we register under both the asset class AND the parent class.
	auto RegisterWidgetIcon = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinWidgetIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
		);
	};
	auto RegisterWidgetThumb = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinWidgetIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
		);
	};

	RegisterWidgetIcon(TEXT("ClassIcon.SeinWidgetBlueprint"));
	RegisterWidgetIcon(TEXT("ClassIcon.SeinUserWidget"));

	RegisterWidgetThumb(TEXT("ClassThumbnail.SeinWidgetBlueprint"));
	RegisterWidgetThumb(TEXT("ClassThumbnail.SeinUserWidget"));

	// ==================== Branding ====================

	// Native PNG is 488x126 (aspect ~3.873). Keep that ratio when registering
	// the brush so anything that samples its ImageSize gets correct proportions.
	StyleSet->Set(
		"SeinARTS.Wordmark",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTSWordmarkVectorized"), TEXT(".png")), FVector2D(244.0f, 63.0f))
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);

	// Load PNG files as UTexture2D for thumbnail renderers (FCanvas can't use Slate file brushes)
	LoadAndCacheIcon(FName(TEXT("SeinUnitIcon92")),       TEXT("SeinUnitIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinAbilityIcon92")),    TEXT("SeinAbilityIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinComponentIcon92")),  TEXT("SeinComponentIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinWidgetIcon92")),     TEXT("SeinWidgetIcon92.png"));
}

UTexture2D* FSeinARTSEditorStyle::LoadAndCacheIcon(const FName& TextureName, const FString& Filename)
{
	const FString FullPath = IconsDir / Filename;
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(FullPath);
	if (Texture)
	{
		Texture->AddToRoot(); // prevent GC
		Texture->UpdateResource(); // ensure render resource is created
		IconTextures.Add(TextureName, Texture);
	}
	return Texture;
}

UTexture2D* FSeinARTSEditorStyle::GetIconTexture(const FName& TextureName)
{
	if (const TObjectPtr<UTexture2D>* Found = IconTextures.Find(TextureName))
	{
		return *Found;
	}
	return nullptr;
}

FString FSeinARTSEditorStyle::GetIconPath(const FString& IconFilename)
{
	return IconsDir / IconFilename;
}

void FSeinARTSEditorStyle::Shutdown()
{
	// Don't touch the UTexture2D pointers during shutdown — the UObject array
	// may already be torn down, and any TObjectPtr dereference will assert.
	// The textures are rooted transients; the process is exiting so leaking is fine.
	IconTextures.Empty();

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
