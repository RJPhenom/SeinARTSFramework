/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationLayer.h
 * @brief   Per-layer nav storage — cells + tiles + optional clearance field.
 *          USeinNavigationGrid owns a TArray<FSeinNavigationLayer> Layers.
 *          Flat maps have one layer; multi-layer maps (overpass/underpass) stack.
 */

#pragma once

#include "CoreMinimal.h"
#include "Grid/SeinCellData.h"
#include "Grid/SeinMapTile.h"
#include "SeinNavigationLayer.generated.h"

/** Palette-index list wrapper (UHT disallows TArray<uint8> directly inside a TMap). */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinCellTagOverflowEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> PaletteIndices;
};

USTRUCT()
struct SEINARTSNAVIGATION_API FSeinNavigationLayer
{
	GENERATED_BODY()

	/** Which cell-data variant is populated below. Single value per layer. */
	UPROPERTY()
	ESeinElevationMode ElevationMode = ESeinElevationMode::None;

	/** Grid width in cells (X). */
	UPROPERTY()
	int32 Width = 0;

	/** Grid height in cells (Y). */
	UPROPERTY()
	int32 Height = 0;

	/** Flat cell storage (populated iff ElevationMode == None). */
	UPROPERTY()
	TArray<FSeinCellData_Flat> CellsFlat;

	/** Height-tier cell storage (populated iff ElevationMode == HeightOnly). */
	UPROPERTY()
	TArray<FSeinCellData_Height> CellsHeight;

	/** Height + slope cell storage (populated iff ElevationMode == HeightSlope). */
	UPROPERTY()
	TArray<FSeinCellData_HeightSlope> CellsHeightSlope;

	/** Overflow for cells with more than 2 tags — keyed by flat cell index. */
	UPROPERTY()
	TMap<int32, FSeinCellTagOverflowEntry> CellTagOverflow;

	/** Tile summaries (default 32×32 cells per tile; size set at bake). */
	UPROPERTY()
	TArray<FSeinMapTile> Tiles;

	/** Tile width in cells (typically 32). */
	UPROPERTY()
	int32 TileSize = 32;

	/** Per-cell clearance = quantized Euclidean distance to nearest blocked cell
	 *  (uint8, computed by distance-transform pass at bake). Single-class MVP;
	 *  per-class layers via USeinARTSCoreSettings::AgentRadiusClasses land later. */
	UPROPERTY()
	TArray<uint8> Clearance;

	/** Z baseline for this layer in world units (used to recover world Z from HeightTier/HeightFixed). */
	UPROPERTY()
	float LayerBaselineZ = 0.0f;

	/** Number of tile columns = (Width + TileSize - 1) / TileSize. */
	FORCEINLINE int32 GetTileWidth() const { return (Width + TileSize - 1) / TileSize; }
	/** Number of tile rows = (Height + TileSize - 1) / TileSize. */
	FORCEINLINE int32 GetTileHeight() const { return (Height + TileSize - 1) / TileSize; }

	/** Total cell count across this layer. */
	FORCEINLINE int32 GetCellCount() const { return Width * Height; }

	/** Allocate cell + tile + clearance arrays for this layer's (Width, Height, mode). */
	void Allocate(int32 InWidth, int32 InHeight, ESeinElevationMode Mode, int32 InTileSize = 32);

	/** Test cell walkability regardless of elevation mode. Bounds-checked. */
	bool IsCellWalkable(int32 CellIndex) const;

	/** Read the cell Flags byte regardless of elevation mode. */
	uint8 GetCellFlags(int32 CellIndex) const;

	/** Set the cell Flags byte regardless of elevation mode. */
	void SetCellFlags(int32 CellIndex, uint8 NewFlags);

	/** Set a single flag bit on a cell. */
	void SetCellFlag(int32 CellIndex, uint8 Flag, bool bValue);

	/** Read per-cell movement cost; 0 = impassable. */
	uint8 GetCellMovementCost(int32 CellIndex) const;

	/** Write per-cell movement cost. */
	void SetCellMovementCost(int32 CellIndex, uint8 Cost);

	/** Read cell clearance (0 if clearance field isn't populated). */
	uint8 GetCellClearance(int32 CellIndex) const;

	/** World-Z for a cell in this layer (baseline + encoded height). Returns baseline for Flat mode. */
	float GetCellWorldZ(int32 CellIndex) const;
};
