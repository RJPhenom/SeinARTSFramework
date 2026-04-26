/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDataDetails.h
 * @brief   Property-type customization for FSeinMovementData. Replaces the
 *          default bitmask UI on NavLayerMask with a combo dropdown whose
 *          labels resolve to the layer names in plugin settings
 *          (USeinARTSCoreSettings::NavLayers).
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinMovementDataDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};
