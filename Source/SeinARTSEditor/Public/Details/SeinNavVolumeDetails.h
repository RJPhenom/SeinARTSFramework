/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolumeDetails.h
 * @brief   Detail customization for ASeinNavVolume — adds a "Rebuild Sein Nav"
 *          button at the top of the details panel that kicks off USeinNavBaker.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSeinNavVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<class UWorld> CachedWorld;

	FReply OnRebuildClicked();
	bool IsRebuildEnabled() const;
};
