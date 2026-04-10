#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationGrid.h"
#include "SeinFlowField.generated.h"

/**
 * A precomputed flow field for efficient group movement.
 * Each cell stores a direction toward the destination via the lowest-cost route.
 */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinFlowField
{
	GENERATED_BODY()

	/** Grid cell of the destination. */
	UPROPERTY()
	FIntPoint Destination = FIntPoint::ZeroValue;

	/**
	 * Per-cell flow direction encoded as 0-7 (8 compass directions).
	 * 255 (DIR_UNREACHABLE) means the cell cannot reach the destination.
	 */
	UPROPERTY()
	TArray<uint8> Directions;

	/** Per-cell integration cost from Dijkstra (lower = closer to destination). */
	UPROPERTY()
	TArray<int32> CostField;

	/** Grid dimensions (cached for index math). */
	int32 Width = 0;
	int32 Height = 0;

	/** Sentinel value for unreachable cells. */
	static constexpr uint8 DIR_UNREACHABLE = 255;

	/** Maximum integration cost sentinel. */
	static constexpr int32 COST_UNREACHABLE = MAX_int32;

	/**
	 * Get the world-space flow direction for a grid cell.
	 * Returns a unit-length FFixedVector (XY only, Z=0) or zero if unreachable.
	 */
	FFixedVector GetFlowDirection(FIntPoint GridPos) const;

	/** True if the cell can reach the destination. */
	bool IsReachable(FIntPoint GridPos) const;

private:
	int32 CellIndex(FIntPoint GridPos) const { return GridPos.Y * Width + GridPos.X; }
	bool IsValidCell(FIntPoint GridPos) const { return GridPos.X >= 0 && GridPos.X < Width && GridPos.Y >= 0 && GridPos.Y < Height; }
};

/**
 * Generates flow fields from a USeinNavigationGrid using Dijkstra integration + direction assignment.
 */
UCLASS()
class SEINARTSNAVIGATION_API USeinFlowFieldGenerator : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to a navigation grid. Must be called before GenerateFlowField. */
	void Initialize(USeinNavigationGrid* InGrid);

	/**
	 * Generate a flow field toward the given destination cell.
	 * Uses Dijkstra's algorithm for integration costs, then assigns per-cell directions.
	 */
	FSeinFlowField GenerateFlowField(FIntPoint Destination);

private:
	UPROPERTY()
	TObjectPtr<USeinNavigationGrid> Grid;
};
