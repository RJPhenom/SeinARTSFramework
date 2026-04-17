/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFixedPointDetails.cpp
 * @brief   Split the FFixedPoint details UI into Integer + Fraction fields,
 *          matching the MakeFixedPointFromParts / BreakFixedPointToParts
 *          convention (int32 high bits, uint32 low bits as int32).
 */

#include "Details/SeinFixedPointDetails.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "SeinFixedPointDetails"

TSharedRef<IPropertyTypeCustomization> FSeinFixedPointDetails::MakeInstance()
{
	return MakeShared<FSeinFixedPointDetails>();
}

void FSeinFixedPointDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// FFixedPoint has a single int64 "Value" member we need to edit as two ints.
	ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(220.f)
	.MaxDesiredWidth(420.f)
	[
		SNew(SHorizontalBox)

		// Integer label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("IntLabel", "Integer"))
			.ToolTipText(LOCTEXT("IntTooltip", "Integer part (high 32 bits)."))
		]

		// Integer entry (plain numeric field — no spin slider)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SNumericEntryBox<int32>)
			.Value(this, &FSeinFixedPointDetails::GetIntegerPart)
			.OnValueCommitted(this, &FSeinFixedPointDetails::OnIntegerCommitted)
		]

		// Fraction label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FracLabel", "Fraction"))
			.ToolTipText(LOCTEXT("FracTooltip",
				"Fractional part (low 32 bits as signed int32). "
				"0 = .0, 2147483648 = .5, 4294967295 = ~.999..."))
		]

		// Fraction entry
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SNumericEntryBox<int32>)
			.Value(this, &FSeinFixedPointDetails::GetFractionPart)
			.OnValueCommitted(this, &FSeinFixedPointDetails::OnFractionCommitted)
		]
	];
}

void FSeinFixedPointDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> /*PropertyHandle*/,
	IDetailChildrenBuilder& /*ChildBuilder*/,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// No child rows — the Value field is fully represented in the header.
}

int64 FSeinFixedPointDetails::GetValue() const
{
	int64 Result = 0;
	if (ValueHandle.IsValid())
	{
		ValueHandle->GetValue(Result);
	}
	return Result;
}

void FSeinFixedPointDetails::SetValue(int64 NewVal) const
{
	if (ValueHandle.IsValid())
	{
		ValueHandle->SetValue(NewVal);
	}
}

TOptional<int32> FSeinFixedPointDetails::GetIntegerPart() const
{
	return static_cast<int32>(GetValue() >> 32);
}

TOptional<int32> FSeinFixedPointDetails::GetFractionPart() const
{
	return static_cast<int32>(static_cast<uint32>(GetValue() & 0xFFFFFFFF));
}

void FSeinFixedPointDetails::SetIntegerPart(int32 NewInt)
{
	const int64 Frac = static_cast<int64>(static_cast<uint32>(GetValue() & 0xFFFFFFFF));
	const int64 NewVal = (static_cast<int64>(NewInt) << 32) | Frac;
	SetValue(NewVal);
}

void FSeinFixedPointDetails::SetFractionPart(int32 NewFrac)
{
	const int64 IntHi = GetValue() & ~static_cast<int64>(0xFFFFFFFF);
	const int64 NewVal = IntHi | static_cast<int64>(static_cast<uint32>(NewFrac));
	SetValue(NewVal);
}

void FSeinFixedPointDetails::OnIntegerCommitted(int32 NewValue, ETextCommit::Type /*CommitType*/)
{
	SetIntegerPart(NewValue);
}

void FSeinFixedPointDetails::OnFractionCommitted(int32 NewValue, ETextCommit::Type /*CommitType*/)
{
	SetFractionPart(NewValue);
}

#undef LOCTEXT_NAMESPACE
