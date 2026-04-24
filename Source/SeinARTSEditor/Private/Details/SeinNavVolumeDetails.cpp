#include "Details/SeinNavVolumeDetails.h"

#include "Volumes/SeinNavVolume.h"
#include "SeinNavigationSubsystem.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

TSharedRef<IDetailCustomization> FSeinNavVolumeDetails::MakeInstance()
{
	return MakeShared<FSeinNavVolumeDetails>();
}

void FSeinNavVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	CachedWorld.Reset();
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		if (ASeinNavVolume* Vol = Cast<ASeinNavVolume>(Obj.Get()))
		{
			CachedWorld = Vol->GetWorld();
			break;
		}
	}

	// Add a "Build" category at the top with the Rebuild button.
	IDetailCategoryBuilder& BuildCategory = DetailBuilder.EditCategory(
		TEXT("Sein Nav Build"), LOCTEXT("SeinNavBuildCategory", "SeinARTS Nav Build"),
		ECategoryPriority::Important);

	BuildCategory.AddCustomRow(LOCTEXT("RebuildRowFilter", "Rebuild"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RebuildDesc",
				"Build the nav grid covering every Sein Nav Volume in this level.\n"
				"Bake runs in the background — cancel or watch progress in the toast."))
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
				.IsEnabled_Raw(this, &FSeinNavVolumeDetails::IsRebuildEnabled)
				.OnClicked_Raw(this, &FSeinNavVolumeDetails::OnRebuildClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RebuildButton", "Bake Navigation"))
				]
			]
		]
	];
}

FReply FSeinNavVolumeDetails::OnRebuildClicked()
{
	if (UWorld* World = CachedWorld.Get())
	{
		USeinNavigationSubsystem::BeginBake(World);
	}
	return FReply::Handled();
}

bool FSeinNavVolumeDetails::IsRebuildEnabled() const
{
	UWorld* World = CachedWorld.Get();
	return World && !USeinNavigationSubsystem::IsBaking(World);
}

#undef LOCTEXT_NAMESPACE
