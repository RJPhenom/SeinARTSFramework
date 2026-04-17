/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFixedPointDetails.h
 * @brief   Property type customization for FFixedPoint. Shows two integer entry
 *          fields (Integer, Fraction) instead of the raw int64 storage, matching
 *          the convention used by MakeFixedPointFromParts / BreakFixedPointToParts.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

class FSeinFixedPointDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> ValueHandle;

	int64 GetValue() const;
	void  SetValue(int64 NewVal) const;

	TOptional<int32> GetIntegerPart() const;
	TOptional<int32> GetFractionPart() const;

	void SetIntegerPart(int32 NewInt);
	void SetFractionPart(int32 NewFrac);

	void OnIntegerCommitted(int32 NewValue, ETextCommit::Type CommitType);
	void OnFractionCommitted(int32 NewValue, ETextCommit::Type CommitType);
};
