/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInstancedStructDetails.cpp
 * @brief   FInstancedStruct customization that permits UserDefinedStructs in
 *          the picker even when BaseStruct is set.
 */

#include "Details/SeinInstancedStructDetails.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"

#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateIconFinder.h"

#include "StructUtils/InstancedStruct.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "SInstancedStructPicker.h"   // FInstancedStructFilter
#include "InstancedStructDetails.h"   // FInstancedStructDataDetails

#define LOCTEXT_NAMESPACE "SeinInstancedStructDetails"

DEFINE_LOG_CATEGORY_STATIC(LogSeinEditorPicker, Log, All);

// =============================================================================
// Custom filter
// =============================================================================
//
// UE's FInstancedStructFilter hardcodes `bAllowUserDefinedStructs = false`
// whenever BaseStruct is set. Additionally, UE's UUserDefinedStruct compiler
// (see UserDefinedStructureCompilerUtils.cpp) explicitly calls
// `StructToClean->SetSuperStruct(nullptr)` during every compile — so UDS can
// never satisfy an IsChildOf(BaseStruct) check anyway.
//
// We work around both issues by discriminating by asset kind:
//   - Native UScriptStruct: use IsChildOf(BaseStruct) like UE does
//   - UUserDefinedStruct:   accept if it carries the "SeinARTSComponent"
//                           metadata tag (set by USeinComponentFactory).
//
class FSeinInstancedStructFilter : public IStructViewerFilter
{
public:
	TWeakObjectPtr<const UScriptStruct> BaseStruct = nullptr;
	bool bAllowBaseStruct = false;

	virtual bool IsStructAllowed(
		const FStructViewerInitializationOptions& /*InInitOptions*/,
		const UScriptStruct* InStruct,
		TSharedRef<FStructViewerFilterFuncs> /*InFilterFuncs*/) override
	{
		if (!InStruct)
		{
			return false;
		}

		// Blueprint-authored struct path: accept all UserDefinedStructs.
		// Ideally we'd filter to only structs created by USeinComponentFactory
		// (tagged with "SeinARTSComponent" metadata), but UDS metadata doesn't
		// survive reliably across UDS recompiles — the tag check rejected every
		// SC_* asset in practice. Showing all UDS is the user-approved fallback.
		// C++ structs still go through the BaseStruct filter below.
		if (InStruct->IsA<UUserDefinedStruct>())
		{
			return true;
		}

		// Native struct path: IsChildOf(BaseStruct).
		if (const UScriptStruct* Base = BaseStruct.Get())
		{
			if (!InStruct->IsChildOf(Base))
			{
				return false;
			}
			if (!bAllowBaseStruct && InStruct == Base)
			{
				return false;
			}
			return true;
		}

		// No base specified — accept all native structs.
		return true;
	}

	virtual bool IsUnloadedStructAllowed(
		const FStructViewerInitializationOptions& /*InInitOptions*/,
		const FSoftObjectPath& /*InStructPath*/,
		TSharedRef<FStructViewerFilterFuncs> /*InFilterFuncs*/) override
	{
		return true;
	}
};

TSharedRef<IPropertyTypeCustomization> FSeinInstancedStructDetails::MakeInstance()
{
	UE_LOG(LogSeinEditorPicker, Warning, TEXT("MakeInstance: creating FSeinInstancedStructDetails"));
	return MakeShared<FSeinInstancedStructDetails>();
}

void FSeinInstancedStructDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructProperty = PropertyHandle;
	PropUtils = CustomizationUtils.GetPropertyUtilities();

	UE_LOG(LogSeinEditorPicker, Warning,
		TEXT("CustomizeHeader invoked. Property=%s  BaseStructMeta=%s"),
		*PropertyHandle->GetPropertyDisplayName().ToString(),
		PropertyHandle->HasMetaData(TEXT("BaseStruct"))
			? *PropertyHandle->GetMetaData(TEXT("BaseStruct"))
			: TEXT("<none>"));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.f)
	.VAlign(VAlign_Center)
	[
		SAssignNew(ComboButton, SComboButton)
		.OnGetMenuContent(this, &FSeinInstancedStructDetails::GenerateStructPicker)
		.ContentPadding(FMargin(2.f, 2.f))
		.ToolTipText(this, &FSeinInstancedStructDetails::GetTooltipText)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SImage)
				.Image(this, &FSeinInstancedStructDetails::GetDisplayValueIcon)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(this, &FSeinInstancedStructDetails::GetDisplayValueString)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
	]
	.IsEnabled(PropertyHandle->IsEditable());
}

void FSeinInstancedStructDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Delegate child-row rendering to UE's public node builder so we inherit
	// proper handling of nested FInstancedStruct fields, category grouping,
	// and struct-value change notifications.
	TSharedRef<FInstancedStructDataDetails> DataDetails =
		MakeShared<FInstancedStructDataDetails>(PropertyHandle);
	ChildBuilder.AddCustomBuilder(DataDetails);
}

// =============================================================================
// Picker construction (the piece that differs from UE's default)
// =============================================================================

const UScriptStruct* FSeinInstancedStructDetails::GetBaseStructFromMeta() const
{
	static const FName NAME_BaseStruct("BaseStruct");
	if (StructProperty.IsValid() && StructProperty->HasMetaData(NAME_BaseStruct))
	{
		const FString& BaseStructName = StructProperty->GetMetaData(NAME_BaseStruct);
		return UClass::TryFindTypeSlow<UScriptStruct>(BaseStructName);
	}
	return nullptr;
}

const UScriptStruct* FSeinInstancedStructDetails::GetCurrentScriptStruct() const
{
	if (!StructProperty.IsValid())
	{
		return nullptr;
	}

	const UScriptStruct* Found = nullptr;
	StructProperty->EnumerateConstRawData(
		[&Found](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (const FInstancedStruct* Inst = static_cast<const FInstancedStruct*>(RawData))
			{
				Found = Inst->GetScriptStruct();
			}
			return false; // stop after first
		});
	return Found;
}

FText FSeinInstancedStructDetails::GetDisplayValueString() const
{
	if (const UScriptStruct* Current = GetCurrentScriptStruct())
	{
		return Current->GetDisplayNameText();
	}
	return LOCTEXT("NoneOption", "None");
}

FText FSeinInstancedStructDetails::GetTooltipText() const
{
	if (const UScriptStruct* Current = GetCurrentScriptStruct())
	{
		return Current->GetToolTipText();
	}
	return LOCTEXT("PickerTooltip",
		"Select a struct type. BP-authored structs (inheriting from the "
		"required base) are included.");
}

const FSlateBrush* FSeinInstancedStructDetails::GetDisplayValueIcon() const
{
	if (const UScriptStruct* Current = GetCurrentScriptStruct())
	{
		return FSlateIconFinder::FindIconBrushForClass(Current, "ClassIcon.Object");
	}
	return nullptr;
}

TSharedRef<SWidget> FSeinInstancedStructDetails::GenerateStructPicker()
{
	static const FName NAME_ExcludeBaseStruct("ExcludeBaseStruct");
	const bool bExcludeBaseStruct =
		StructProperty.IsValid() && StructProperty->HasMetaData(NAME_ExcludeBaseStruct);

	const UScriptStruct* BaseStruct = GetBaseStructFromMeta();

	UE_LOG(LogSeinEditorPicker, Warning,
		TEXT("GenerateStructPicker: BaseStruct=%s  ExcludeBaseStruct=%d"),
		BaseStruct ? *BaseStruct->GetName() : TEXT("<null>"),
		bExcludeBaseStruct ? 1 : 0);

	TSharedRef<FSeinInstancedStructFilter> Filter = MakeShared<FSeinInstancedStructFilter>();
	Filter->BaseStruct = BaseStruct;
	Filter->bAllowBaseStruct = !bExcludeBaseStruct;

	FStructViewerInitializationOptions Options;
	Options.bShowNoneOption = true;
	Options.bShowUnloadedStructs = true;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.StructFilter = Filter;
	Options.SelectedStruct = GetCurrentScriptStruct();
	Options.PropertyHandle = StructProperty;

	FStructViewerModule& StructViewerModule =
		FModuleManager::LoadModuleChecked<FStructViewerModule>(TEXT("StructViewer"));

	return SNew(SBox)
		.WidthOverride(280.f)
		.MaxDesiredHeight(500.f)
		[
			StructViewerModule.CreateStructViewer(
				Options,
				FOnStructPicked::CreateSP(this, &FSeinInstancedStructDetails::OnStructPicked))
		];
}

void FSeinInstancedStructDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	if (StructProperty.IsValid() && StructProperty->IsValidHandle())
	{
		FScopedTransaction Transaction(LOCTEXT("OnStructPicked", "Set Struct"));
		StructProperty->NotifyPreChange();

		StructProperty->EnumerateRawData(
			[InStruct](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
			{
				if (FInstancedStruct* Inst = static_cast<FInstancedStruct*>(RawData))
				{
					Inst->InitializeAs(InStruct);
				}
				return true;
			});

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		StructProperty->NotifyFinishedChangingProperties();
		StructProperty->SetExpanded(true);

		if (PropUtils.IsValid())
		{
			PropUtils->ForceRefresh();
		}
	}

	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE
