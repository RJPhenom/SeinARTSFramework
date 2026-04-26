/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsDataDetails.h
 * @brief   Property-type customization for FSeinExtentsData. Replaces the
 *          default bitmask UI on BlockedNavLayerMask + BlockedFogLayerMask
 *          with a combo dropdown whose labels resolve to the layer names
 *          configured in plugin settings (USeinARTSCoreSettings::NavLayers /
 *          VisionLayers).
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinExtentsDataDetails : public IPropertyTypeCustomization
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
