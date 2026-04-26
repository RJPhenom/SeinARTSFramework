/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionStampDetails.cpp
 */

#include "Details/SeinVisionStampDetails.h"
#include "Details/SeinLayerMaskUI.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "SeinVisionStampDetails"

TSharedRef<IPropertyTypeCustomization> FSeinVisionStampDetails::MakeInstance()
{
	return MakeShared<FSeinVisionStampDetails>();
}

void FSeinVisionStampDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// As an array element, the header normally shows "Index [N]" — preserve
	// that. Only skip when the property is being inlined via
	// ShowOnlyInnerProperties (rare for this type, defensive).
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

void FSeinVisionStampDetails::CustomizeChildren(
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

		// LayerMask → labeled combo using plugin-settings vision layer names.
		if (PropName == TEXT("LayerMask"))
		{
			SeinLayerMaskUI::AddLayerMaskRow(
				ChildHandle.ToSharedRef(), ChildBuilder,
				&SeinLayerMaskUI::GetFogLayerLabel,
				LOCTEXT("LayerMaskRow", "Layer Mask"));
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
