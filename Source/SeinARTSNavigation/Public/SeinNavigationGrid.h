#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinNavigationGrid.generated.h"

/**
 * Deterministic grid-based navigation data for RTS pathfinding.
 * Can be serialized as an asset or generated at runtime.
 * Uses fixed-point math for lockstep determinism.
 */
UCLASS(BlueprintType)
class SEINARTSNAVIGATION_API USeinNavigationGrid : public UObject
{
	GENERATED_BODY()

public:
	/** Number of cells along the X axis. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	int32 GridWidth = 0;

	/** Number of cells along the Y axis. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	int32 GridHeight = 0;

	/** World units per cell edge. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	FFixedPoint CellSize;

	/** World-space origin (bottom-left corner of grid). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridOrigin;

	/** Per-cell walkability / movement cost. 0 = blocked, 1-255 = walkable with cost. */
	UPROPERTY()
	TArray<uint8> Walkability;

	/** Per-cell terrain type tag. */
	UPROPERTY()
	TArray<FGameplayTag> TerrainTypes;

	/** Dynamic obstacle overlay (runtime only, not serialized). */
	TBitArray<> DynamicBlocked;

	// -------------------------------------------------------------------
	// Initialization
	// -------------------------------------------------------------------

	/** Allocate grid arrays and set all cells walkable (cost 1) by default. */
	void InitializeGrid(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin);

	// -------------------------------------------------------------------
	// Coordinate conversions
	// -------------------------------------------------------------------

	/** Convert a world-space position to a grid cell coordinate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FIntPoint WorldToGrid(const FFixedVector& WorldPos) const;

	/** Convert a grid cell to its world-space origin (bottom-left corner). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridToWorld(FIntPoint GridPos) const;

	/** Convert a grid cell to the world-space center of that cell. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridToWorldCenter(FIntPoint GridPos) const;

	// -------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------

	/** True if the grid coordinate is within bounds. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	bool IsValidCell(FIntPoint GridPos) const;

	/** True if the cell is walkable (static cost > 0 AND not dynamically blocked). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	bool IsWalkable(FIntPoint GridPos) const;

	/** Return the static movement cost for a cell. 0 means blocked. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	uint8 GetMovementCost(FIntPoint GridPos) const;

	/** Return the terrain type tag for a cell. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FGameplayTag GetTerrainType(FIntPoint GridPos) const;

	// -------------------------------------------------------------------
	// Dynamic obstacles
	// -------------------------------------------------------------------

	void MarkCellBlocked(FIntPoint GridPos);
	void MarkCellUnblocked(FIntPoint GridPos);
	void MarkAreaBlocked(FIntPoint Min, FIntPoint Max);
	void MarkAreaUnblocked(FIntPoint Min, FIntPoint Max);

	// -------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------

	/** Convert a 2D grid coordinate to a 1D array index. */
	int32 CellIndex(FIntPoint GridPos) const;

	/** True if Index is a valid flat index into the grid arrays. */
	bool IsValidIndex(int32 Index) const;

	/** Return valid neighboring cells (4 or 8 connectivity). */
	TArray<FIntPoint> GetNeighbors(FIntPoint GridPos, bool bIncludeDiagonals = true) const;
};
