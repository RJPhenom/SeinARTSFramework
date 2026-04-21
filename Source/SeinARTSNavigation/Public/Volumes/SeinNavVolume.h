/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolume.h
 * @brief   Nav-bake bounds actor. Parallels UE's ANavMeshBoundsVolume — drop it
 *          into a level, scale it, click Rebuild Sein Nav. Multiple per level
 *          union into one USeinNavigationGridAsset (DESIGN.md §13).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Grid/SeinCellData.h"
#include "SeinNavVolume.generated.h"

class USeinNavigationGridAsset;
class USeinNavDebugRenderingComponent;

UCLASS(meta = (DisplayName = "Sein Nav Volume"))
class SEINARTSNAVIGATION_API ASeinNavVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinNavVolume(const FObjectInitializer& ObjectInitializer);

	// --- Per-volume bake config (overrides plugin-setting defaults when bOverride* is set) ---

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides",
		meta = (EditCondition = "bOverrideCellSize", ClampMin = "10.0"))
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides")
	bool bOverrideElevationMode = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides",
		meta = (EditCondition = "bOverrideElevationMode"))
	ESeinElevationMode ElevationMode = ESeinElevationMode::None;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides")
	bool bOverrideLayerSeparation = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Bake Overrides",
		meta = (EditCondition = "bOverrideLayerSeparation", ClampMin = "1.0"))
	float LayerSeparation = 100.0f;

	/**
	 * Baked grid asset for this level. Assigned by the bake pipeline; shared
	 * across all NavVolumes on the level (last-baked wins). Runtime loads this
	 * via USeinNavigationSubsystem.
	 */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation|Output")
	TObjectPtr<USeinNavigationGridAsset> BakedGridAsset;

	// --- Helpers ---

	/** World-space AABB of this volume's brush. */
	FBox GetVolumeWorldBounds() const;

	/** Resolved cell size after plugin-setting + override layering. */
	float GetResolvedCellSize() const;

	/** Resolved elevation mode after plugin-setting + override layering. */
	ESeinElevationMode GetResolvedElevationMode() const;

	/** Resolved layer separation after plugin-setting + override layering. */
	float GetResolvedLayerSeparation() const;

	/**
	 * Debug rendering component — paints baked nav cells (green walkable / red
	 * blocked) in-viewport when ShowFlags.Navigation is set (toggled by UE's 'P'
	 * key, `showflag.navigation`, or `SeinARTS.Navigation.Debug`). Constructed
	 * only when UE_ENABLE_DEBUG_DRAWING is on — pointer stays null in shipping.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "SeinARTS|Navigation|Debug")
	TObjectPtr<USeinNavDebugRenderingComponent> DebugRenderComponent;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
