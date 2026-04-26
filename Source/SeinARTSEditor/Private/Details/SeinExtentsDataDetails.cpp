/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsDataDetails.cpp
 */

#include "Details/SeinExtentsDataDetails.h"
#include "Details/SeinLayerMaskUI.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "SeinExtentsDataDetails"

TSharedRef<IPropertyTypeCustomization> FSeinExtentsDataDetails::MakeInstance()
{
	return MakeShared<FSeinExtentsDataDetails>();
}

void FSeinExtentsDataDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Mirror UE's StructUtils customization: when the owning property carries
	// ShowOnlyInnerProperties (designer convenience to inline a struct's
	// fields into the parent component's category), skip HeaderRow entirely.
	// Without this, our customization adds a "Data" wrapper row that
	// ShowOnlyInnerProperties was supposed to suppress.
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

void FSeinExtentsDataDetails::CustomizeChildren(
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

		const FName PropName = ChildHandle->GetProperty()->GetFName();

		// Both layer-mask fields get the named-dropdown treatment so a
		// designer renaming "N0" → "Amphibious" / "Thermal" in plugin
		// settings sees the live name in the picker without touching
		// component data.
		if (PropName == TEXT("BlockedNavLayerMask"))
		{
			SeinLayerMaskUI::AddLayerMaskRow(
				ChildHandle.ToSharedRef(), ChildBuilder,
				&SeinLayerMaskUI::GetNavLayerLabel,
				LOCTEXT("BlockedNavMaskRow", "Blocked Nav Layer Mask"));
		}
		else if (PropName == TEXT("BlockedFogLayerMask"))
		{
			SeinLayerMaskUI::AddLayerMaskRow(
				ChildHandle.ToSharedRef(), ChildBuilder,
				&SeinLayerMaskUI::GetFogLayerLabel,
				LOCTEXT("BlockedFogMaskRow", "Blocked Fog Layer Mask"));
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
