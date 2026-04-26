/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionDataDetails.cpp
 */

#include "Details/SeinVisionDataDetails.h"

#include "Settings/PluginSettings.h"
#include "Data/SeinVisionLayerDefinition.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SeinVisionDataDetails"

namespace
{
	/** Resolve label for an EVNNNNNN bit using plugin settings for N bits.
	 *  Bit 0 → Explored, Bit 1 → Visible (V/Normal). Bits 2..7 look up
	 *  `USeinARTSCoreSettings::VisionLayers[bit-2]`:
	 *    enabled + named → the layer's LayerName
	 *    disabled / unnamed / out-of-range → "None"
	 *  Reading live each call means renaming a slot in settings updates
	 *  the dropdown text without an editor restart. */
	static FText GetBitLabel(int32 BitIdx)
	{
		if (BitIdx == 0) return LOCTEXT("Explored", "Explored");
		if (BitIdx == 1) return LOCTEXT("Visible", "Visible");

		const int32 SlotIdx = BitIdx - 2;
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (Settings && Settings->VisionLayers.IsValidIndex(SlotIdx))
		{
			const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[SlotIdx];
			if (Def.bEnabled && !Def.LayerName.IsNone())
			{
				return FText::FromName(Def.LayerName);
			}
		}
		return LOCTEXT("None", "None");
	}

	/** Summary text shown on the combo button — comma-separated list of
	 *  currently-set bits, or "(none)" when the mask is 0. */
	static FText GetSummaryText(TSharedPtr<IPropertyHandle> MaskHandle)
	{
		if (!MaskHandle.IsValid()) return FText::GetEmpty();
		uint8 Value = 0;
		MaskHandle->GetValue(Value);
		if (Value == 0) return LOCTEXT("NoFlags", "(none)");
		TArray<FString> Names;
		for (int32 Bit = 0; Bit < 8; ++Bit)
		{
			if ((Value & (1u << Bit)) != 0)
			{
				Names.Add(GetBitLabel(Bit).ToString());
			}
		}
		return FText::FromString(FString::Join(Names, TEXT(", ")));
	}

	/** Build the dropdown popup — one row per bit with a checkbox bound
	 *  to that bit's state and a dynamic label. */
	static TSharedRef<SWidget> BuildBitmaskMenu(TSharedPtr<IPropertyHandle> MaskHandle)
	{
		TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);
		for (int32 Bit = 0; Bit < 8; ++Bit)
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(8.f, 3.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([MaskHandle, Bit]()
					{
						if (!MaskHandle.IsValid()) return ECheckBoxState::Unchecked;
						uint8 V = 0;
						MaskHandle->GetValue(V);
						return ((V & (1u << Bit)) != 0) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([MaskHandle, Bit](ECheckBoxState State)
					{
						if (!MaskHandle.IsValid()) return;
						uint8 V = 0;
						MaskHandle->GetValue(V);
						const uint8 BitMask = static_cast<uint8>(1u << Bit);
						const uint8 NewV = (State == ECheckBoxState::Checked)
							? (V | BitMask)
							: (V & ~BitMask);
						MaskHandle->SetValue(NewV);
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([Bit]() { return GetBitLabel(Bit); })
				]
			];
		}
		return VBox;
	}

	/** Render EmissionLayerMask as a combo dropdown with dynamic labels. */
	static void AddEmissionMaskRow(TSharedRef<IPropertyHandle> MaskHandle,
		IDetailChildrenBuilder& ChildBuilder)
	{
		ChildBuilder.AddCustomRow(LOCTEXT("EmissionRowFilter", "Emission Layer Mask"))
		.NameContent()
		[
			MaskHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		.MaxDesiredWidth(420.f)
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(6.f, 2.f))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([MaskHandle]() { return GetSummaryText(MaskHandle); })
			]
			.OnGetMenuContent_Lambda([MaskHandle]()
			{
				return BuildBitmaskMenu(MaskHandle);
			})
		];
	}
}

TSharedRef<IPropertyTypeCustomization> FSeinVisionDataDetails::MakeInstance()
{
	return MakeShared<FSeinVisionDataDetails>();
}

void FSeinVisionDataDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Skip the header row when the owning property uses ShowOnlyInnerProperties
	// (USeinVisionComponent::Data). Without this, the customization would
	// re-introduce a "Data" wrapper that the inline meta was supposed to skip.
	const bool bShowOnlyInner = PropertyHandle->HasMetaData(TEXT("ShowOnlyInnerProperties"));
	if (!bShowOnlyInner)
	{
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
	}
}

void FSeinVisionDataDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid() || !ChildHandle->GetProperty()) continue;

		// EmissionLayerMask → custom dropdown with dynamic labels (same
		// labels feed FSeinVisionStamp::LayerMask too — both are EVNNNNNN
		// bitmask fields and pick up the BitmaskEnum metadata directly).
		// Everything else (EyeHeight, Stamps array) → default UI.
		if (ChildHandle->GetProperty()->GetFName() == TEXT("EmissionLayerMask"))
		{
			AddEmissionMaskRow(ChildHandle.ToSharedRef(), ChildBuilder);
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
