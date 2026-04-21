/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationGridAsset.h
 * @brief   Serialized companion uasset holding baked nav data for a level.
 *          Referenced by the level (via ASeinNavVolume + level actor) and loaded
 *          at runtime by USeinNavigationSubsystem. Replay-safe: bake happens
 *          at edit time, no runtime re-bake.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "Grid/SeinCellData.h"
#include "Grid/SeinNavigationLayer.h"
#include "Grid/SeinAbstractGraph.h"
#include "SeinNavigationGridAsset.generated.h"

/**
 * Baked nav data. Populated by USeinNavBaker at edit time, applied to
 * USeinNavigationGrid at runtime.
 */
UCLASS(BlueprintType)
class SEINARTSNAVIGATION_API USeinNavigationGridAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Grid width in cells (X). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	int32 GridWidth = 0;

	/** Grid height in cells (Y). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	int32 GridHeight = 0;

	/** World units per cell. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	float CellSize = 100.0f;

	/** Bottom-left world origin of layer 0. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	FVector GridOrigin = FVector::ZeroVector;

	/** Tile size in cells (typically 32). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	int32 TileSize = 32;

	/** Elevation mode — same for all layers in this grid. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Grid")
	ESeinElevationMode ElevationMode = ESeinElevationMode::None;

	/** Per-map shared tag palette. Index 0 = null. */
	UPROPERTY()
	TArray<FGameplayTag> CellTagPalette;

	/** Baked layers. Layer 0 = ground; higher indices stack upward. */
	UPROPERTY()
	TArray<FSeinNavigationLayer> Layers;

	/** HPA*-style abstract graph spanning all layers + navlinks. */
	UPROPERTY()
	FSeinAbstractGraph AbstractGraph;

	/** Baked per-navlink records (toggleable at runtime via SeinSetNavLinkEnabled). */
	UPROPERTY()
	TArray<FSeinNavLinkRecord> NavLinks;

	/** Metadata: bake timestamp + machine for debugging cross-machine drift. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Bake Info")
	FDateTime BakeTimestamp;

	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Bake Info")
	FString BakeMachine;

	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|Bake Info")
	int32 BakeCellCount = 0;
};
