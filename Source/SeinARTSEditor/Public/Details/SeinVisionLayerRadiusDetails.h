/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionLayerRadiusDetails.h
 * @brief   Property-type customization for `FSeinVisionLayerRadius` — the
 *          per-slot entry of `FSeinVisionData::LayerSlots`. Labels each
 *          array element with its corresponding layer name from
 *          `USeinARTSCoreSettings::VisionLayers[Index]` so designers see
 *          "Thermal" / "Camouflage" / "None" instead of "Element 0..5".
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinVisionLayerRadiusDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> CachedHandle;

	/** Label resolver — reads `USeinARTSCoreSettings::VisionLayers[index]`
	 *  at every refresh so renaming a slot in settings updates the UI
	 *  without an editor restart. */
	FText GetSlotLabel() const;

	/** Tooltip for the label — includes slot index + bit number so
	 *  designers wiring shaders / BPFLs can cross-reference cleanly. */
	FText GetSlotTooltip() const;
};
