/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDataDetails.cpp
 */

#include "Details/SeinMovementDataDetails.h"
#include "Details/SeinLayerMaskUI.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "SeinMovementDataDetails"

TSharedRef<IPropertyTypeCustomization> FSeinMovementDataDetails::MakeInstance()
{
	return MakeShared<FSeinMovementDataDetails>();
}

void FSeinMovementDataDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Skip the header row when the owning property uses ShowOnlyInnerProperties
	// (USeinMovementComponent::Data). See FSeinExtentsDataDetails for rationale.
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

void FSeinMovementDataDetails::CustomizeChildren(
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

		if (PropName == TEXT("NavLayerMask"))
		{
			SeinLayerMaskUI::AddLayerMaskRow(
				ChildHandle.ToSharedRef(), ChildBuilder,
				&SeinLayerMaskUI::GetNavLayerLabel,
				LOCTEXT("NavLayerMaskRow", "Nav Layer Mask"));
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
