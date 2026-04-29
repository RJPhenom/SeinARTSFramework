/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolumeDetails.h
 * @brief   Detail customization for ASeinFogOfWarVolume — adds the "Bake Fog
 *          Of War" button to the volume's details panel and forwards to the
 *          active USeinFogOfWar subclass via `CustomizeVolumeDetails` so
 *          impls can extend the panel with their own rows.
 *
 *          Lives in SeinARTSFogOfWar (private/editor) so the system stays
 *          self-contained — a team replacing the fog module without using
 *          the framework's editor module still gets the bake button
 *          registered automatically when the fog module loads.
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
