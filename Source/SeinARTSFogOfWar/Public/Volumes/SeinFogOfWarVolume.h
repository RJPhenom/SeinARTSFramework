/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolume.h
 * @brief   Fog-of-war bake-bounds volume. Drop into a level, scale it, click
 *          "Bake Fog Of War" on the details panel. Multiple volumes union
 *          into one baked asset (last-baked wins).
 *
 *          Fully decoupled from navigation. A project that runs custom nav
 *          + stock fog lands two volumes on the level: ASeinNavVolume and
 *          ASeinFogOfWarVolume, at whatever independent cell sizes the two
 *          subsystems want. Neither reads the other.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "SeinFogOfWarVolume.generated.h"

class USeinFogOfWarAsset;
class USeinFogOfWarDebugComponent;

UCLASS(meta = (DisplayName = "Sein Fog Of War Volume"))
class SEINARTSFOGOFWAR_API ASeinFogOfWarVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinFogOfWarVolume(const FObjectInitializer& ObjectInitializer);

	/** Override the project-wide default fog cell size. When false, the plugin
	 *  settings' `VisionCellSize` is used. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War|Overrides")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War|Overrides",
		meta = (EditCondition = "bOverrideCellSize", ClampMin = "10.0"))
	float CellSize = 400.0f;

	/** Baked fog data for this level. Assigned by the bake pipeline; shared
	 *  across all ASeinFogOfWarVolumes on the level (last-baked wins).
	 *  Polymorphic — concrete type depends on the active USeinFogOfWar
	 *  subclass. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War|Output")
	TObjectPtr<USeinFogOfWarAsset> BakedAsset;

	/** Scene-proxy-backed cell viz. Driven by `ShowFlags.FogOfWar` /
	 *  `SeinARTS.Debug.ShowFogOfWar`; null in shipping. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "SeinARTS|Fog Of War|Debug")
	TObjectPtr<USeinFogOfWarDebugComponent> DebugComponent;

	/** World-space AABB of this volume's brush. */
	FBox GetVolumeWorldBounds() const;

	/** Per-volume cell size with plugin-settings fallback. */
	float GetResolvedCellSize() const;
};
