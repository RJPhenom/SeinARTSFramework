#include "Editor/SeinFogOfWarVolumeDetails.h"

#include "Volumes/SeinFogOfWarVolume.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "SeinARTSFogOfWarEditor"

TSharedRef<IDetailCustomization> FSeinFogOfWarVolumeDetails::MakeInstance()
{
	return MakeShared<FSeinFogOfWarVolumeDetails>();
}

void FSeinFogOfWarVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	CachedWorld.Reset();
	ASeinFogOfWarVolume* CachedVolume = nullptr;
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		if (ASeinFogOfWarVolume* Vol = Cast<ASeinFogOfWarVolume>(Obj.Get()))
		{
			CachedWorld = Vol->GetWorld();
			CachedVolume = Vol;
			break;
		}
	}

	IDetailCategoryBuilder& BuildCategory = DetailBuilder.EditCategory(
		TEXT("Sein Fog Of War Build"),
		LOCTEXT("SeinFogOfWarBuildCategory", "SeinARTS Fog Of War Build"),
		ECategoryPriority::Important);

	BuildCategory.AddCustomRow(LOCTEXT("BakeRowFilter", "Bake"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BakeDesc",
				"Bake the fog-of-war grid covering every Sein Fog Of War Volume in this level.\n"
				"Per-cell downward traces capture terrain height + detect static sight blockers\n"
				"(walls, buildings). Output is serialized to /Game/FogOfWarData/ and auto-\n"
				"assigned to every volume's Baked Asset."))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(SBox)
			.HeightOverride(32.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.IsEnabled_Raw(this, &FSeinFogOfWarVolumeDetails::IsBakeEnabled)
				.OnClicked_Raw(this, &FSeinFogOfWarVolumeDetails::OnBakeClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeButton", "Bake Fog Of War"))
				]
			]
		]
	];

	// Forward to the active fog impl so subclasses can extend the panel.
	// Default impl is a no-op; the framework's bake button above stays first
	// for consistent UX across nav impls.
	if (UWorld* World = CachedWorld.Get())
	{
		if (USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(World))
		{
			Fog->CustomizeVolumeDetails(DetailBuilder, CachedVolume);
		}
	}
}

FReply FSeinFogOfWarVolumeDetails::OnBakeClicked()
{
	if (UWorld* World = CachedWorld.Get())
	{
		USeinFogOfWarSubsystem::BeginBake(World);
	}
	return FReply::Handled();
}

bool FSeinFogOfWarVolumeDetails::IsBakeEnabled() const
{
	UWorld* World = CachedWorld.Get();
	return World && !USeinFogOfWarSubsystem::IsBaking(World);
}

#undef LOCTEXT_NAMESPACE
