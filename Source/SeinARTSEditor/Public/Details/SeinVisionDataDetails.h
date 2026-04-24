/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionDataDetails.h
 * @brief   Property-type customization for `FSeinVisionData` — renders the
 *          EmissionLayerMask bitfield as a dropdown whose rows are labeled
 *          with the project's configured layer names (Explored / Visible /
 *          <VisionLayers[0].LayerName> / ...) instead of the static N0..N5
 *          that the UENUM-Bitflags picker shows. All other fields use
 *          default rendering.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinVisionDataDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};
