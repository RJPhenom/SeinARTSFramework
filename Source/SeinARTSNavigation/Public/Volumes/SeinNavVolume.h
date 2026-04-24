/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolume.h
 * @brief   Nav-bake bounds volume. Parallels UE's ANavMeshBoundsVolume — drop
 *          it into a level, scale it, click "Rebuild Sein Nav" on the details
 *          panel. Multiple volumes union into one baked asset.
 *
 *          Decoupling: this actor knows nothing about how nav is baked or
 *          queried. It just defines bounds + holds a polymorphic baked-asset
 *          reference. The active USeinNavigation subclass (see plugin settings)
 *          owns bake semantics.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "SeinNavVolume.generated.h"

class USeinNavigationAsset;
class USeinNavDebugComponent;

UCLASS(meta = (DisplayName = "Sein Nav Volume"))
class SEINARTSNAVIGATION_API ASeinNavVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinNavVolume(const FObjectInitializer& ObjectInitializer);

	/** Override the project-wide default cell size. When false, the plugin
	 *  settings' `DefaultCellSize` is used. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Overrides")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Overrides",
		meta = (EditCondition = "bOverrideCellSize", ClampMin = "10.0"))
	float CellSize = 100.0f;

	/** Maximum vertical step (world units) an agent can traverse between
	 *  adjacent cells. Blocks "jumps" across gaps where two cells happen to
	 *  have matching surface Zs but no physical geometry bridging them:
	 *  ramp-top → cube-top across thin air is rejected because the midpoint
	 *  surface (ground below) drops more than this value. Typical value is a
	 *  fraction of a cell (~30-60 cm for a 100 cm grid). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Overrides")
	bool bOverrideMaxStepHeight = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Overrides",
		meta = (EditCondition = "bOverrideMaxStepHeight", ClampMin = "0.0"))
	float MaxStepHeight = 50.0f;

	/** Baked nav data for this level. Assigned by the bake pipeline; shared
	 *  across all NavVolumes on the level (last-baked wins). Polymorphic —
	 *  concrete type depends on the active USeinNavigation subclass. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Output")
	TObjectPtr<USeinNavigationAsset> BakedAsset;

	/** Scene-proxy-backed cell viz. Driven by `ShowFlags.Navigation` /
	 *  `SeinARTS.Debug.ShowNavigation`; null in shipping. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "SeinARTS|Navigation|Debug")
	TObjectPtr<USeinNavDebugComponent> DebugComponent;

	/** World-space AABB of this volume's brush. */
	FBox GetVolumeWorldBounds() const;

	/** Per-volume cell size with plugin-settings fallback. */
	float GetResolvedCellSize() const;

	/** Per-volume max-step-height with plugin-settings fallback
	 *  (`USeinARTSCoreSettings::DefaultMaxStepHeight`). */
	float GetResolvedMaxStepHeight() const;
};
