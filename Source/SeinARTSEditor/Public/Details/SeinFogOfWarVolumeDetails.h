/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolumeDetails.h
 * @brief   Detail customization for ASeinFogOfWarVolume — adds a "Bake Fog
 *          Of War" button at the top of the details panel that kicks off
 *          USeinFogOfWarSubsystem::BeginBake. Mirrors FSeinNavVolumeDetails.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSeinFogOfWarVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<class UWorld> CachedWorld;

	FReply OnBakeClicked();
	bool IsBakeEnabled() const;
};
