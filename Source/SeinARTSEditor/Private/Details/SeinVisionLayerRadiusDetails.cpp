/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionLayerRadiusDetails.cpp
 */

#include "Details/SeinVisionLayerRadiusDetails.h"

#include "Settings/PluginSettings.h"
#include "Data/SeinVisionLayerDefinition.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "SeinVisionLayerRadiusDetails"

TSharedRef<IPropertyTypeCustomization> FSeinVisionLayerRadiusDetails::MakeInstance()
{
	return MakeShared<FSeinVisionLayerRadiusDetails>();
}

void FSeinVisionLayerRadiusDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	CachedHandle = PropertyHandle;

	TSharedPtr<IPropertyHandle> EnabledHandle = PropertyHandle->GetChildHandle(TEXT("bEnabled"));
	TSharedPtr<IPropertyHandle> RadiusHandle  = PropertyHandle->GetChildHandle(TEXT("Radius"));
	TSharedPtr<IPropertyHandle> RadiusValueHandle = RadiusHandle.IsValid()
		? RadiusHandle->GetChildHandle(TEXT("Value")) : nullptr;

	// If the struct is used outside an array (or children can't resolve),
	// fall through to default UI.
	if (!EnabledHandle.IsValid() || !RadiusHandle.IsValid() || !RadiusValueHandle.IsValid())
	{
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			PropertyHandle->CreatePropertyValueWidget()
		];
		return;
	}

	// FFixedPoint value shared getter/setter. Mirrors FSeinFixedPointDetails:
	// high 32 bits = integer part, low 32 bits (as signed int32) = fraction.
	// We can't reuse FSeinFixedPointDetails via CreatePropertyValueWidget
	// here because nested property-type customizations don't bind correctly
	// when invoked from inside another customization — the rendered widget
	// comes back empty. Inlining the int/frac pair is the reliable fix.
	auto GetInt = [RadiusValueHandle]() -> TOptional<int32>
	{
		int64 V = 0;
		RadiusValueHandle->GetValue(V);
		return static_cast<int32>(V >> 32);
	};
	auto GetFrac = [RadiusValueHandle]() -> TOptional<int32>
	{
		int64 V = 0;
		RadiusValueHandle->GetValue(V);
		return static_cast<int32>(static_cast<uint32>(V & 0xFFFFFFFF));
	};
	auto SetInt = [RadiusValueHandle](int32 NewInt, ETextCommit::Type)
	{
		int64 V = 0;
		RadiusValueHandle->GetValue(V);
		const int64 Frac = static_cast<int64>(static_cast<uint32>(V & 0xFFFFFFFF));
		RadiusValueHandle->SetValue((static_cast<int64>(NewInt) << 32) | Frac);
	};
	auto SetFrac = [RadiusValueHandle](int32 NewFrac, ETextCommit::Type)
	{
		int64 V = 0;
		RadiusValueHandle->GetValue(V);
		const int64 IntHi = V & ~static_cast<int64>(0xFFFFFFFF);
		RadiusValueHandle->SetValue(IntHi | static_cast<int64>(static_cast<uint32>(NewFrac)));
	};

	// Compact single-row UI: [bEnabled checkbox] Label     Integer [#] Fraction [#]
	// Label text comes from plugin settings; "None" when the slot is
	// unconfigured / disabled in settings.
	HeaderRow
	.NameContent()
	.MinDesiredWidth(180.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			EnabledHandle->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &FSeinVisionLayerRadiusDetails::GetSlotLabel)
			.ToolTipText(this, &FSeinVisionLayerRadiusDetails::GetSlotTooltip)
		]
	]
	.ValueContent()
	.MinDesiredWidth(260.f)
	.MaxDesiredWidth(420.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("IntLabel", "Integer"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SNumericEntryBox<int32>)
			.Value_Lambda(GetInt)
			.OnValueCommitted_Lambda(SetInt)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FracLabel", "Fraction"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SNumericEntryBox<int32>)
			.Value_Lambda(GetFrac)
			.OnValueCommitted_Lambda(SetFrac)
		]
	];
}

void FSeinVisionLayerRadiusDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> /*PropertyHandle*/,
	IDetailChildrenBuilder& /*ChildBuilder*/,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// No child rows — both bEnabled and Radius live in the header.
}

FText FSeinVisionLayerRadiusDetails::GetSlotLabel() const
{
	if (!CachedHandle.IsValid()) return LOCTEXT("UnknownSlot", "?");

	const int32 Index = CachedHandle->GetIndexInArray();
	if (Index < 0)
	{
		// Used as a plain struct (not in an array) — no settings mapping.
		return LOCTEXT("LayerRadiusLabel", "Vision Layer");
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->VisionLayers.IsValidIndex(Index))
	{
		// Settings array missing the slot — render index only.
		return FText::Format(LOCTEXT("SlotIndexed", "Slot {0}"), Index);
	}

	const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[Index];
	if (!Def.bEnabled || Def.LayerName.IsNone())
	{
		return LOCTEXT("SlotUnused", "None");
	}
	return FText::FromName(Def.LayerName);
}

FText FSeinVisionLayerRadiusDetails::GetSlotTooltip() const
{
	if (!CachedHandle.IsValid()) return FText::GetEmpty();

	const int32 Index = CachedHandle->GetIndexInArray();
	if (Index < 0) return LOCTEXT("LayerRadiusStandaloneTooltip",
		"Per-layer vision entry.");

	// N-bit mapping: slot N → EVNNNNNN bit (2 + N). Calling out the bit
	// helps designers cross-reference shader / BPFL code.
	const int32 BitIndex = 2 + Index;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->VisionLayers.IsValidIndex(Index))
	{
		return FText::Format(
			LOCTEXT("TooltipNoSettings",
				"Slot {0} — bit {1}. Plugin settings has no entry for this slot; "
				"configure in Project Settings > Plugins > SeinARTS > Vision > Vision Layers."),
			Index, BitIndex);
	}

	const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[Index];
	if (!Def.bEnabled)
	{
		return FText::Format(
			LOCTEXT("TooltipDisabled",
				"Slot {0} — bit {1}. Disabled in plugin settings. Enable + name this slot in "
				"Project Settings > Plugins > SeinARTS > Vision > Vision Layers before authoring here."),
			Index, BitIndex);
	}
	if (Def.LayerName.IsNone())
	{
		return FText::Format(
			LOCTEXT("TooltipUnnamed",
				"Slot {0} — bit {1}. Enabled but unnamed in plugin settings; set a LayerName there."),
			Index, BitIndex);
	}

	return FText::Format(
		LOCTEXT("TooltipLayer",
			"Layer {0} — slot {1}, bit {2}. Check to have this vision source stamp the {0} "
			"layer at the configured radius."),
		FText::FromName(Def.LayerName), Index, BitIndex);
}

#undef LOCTEXT_NAMESPACE
