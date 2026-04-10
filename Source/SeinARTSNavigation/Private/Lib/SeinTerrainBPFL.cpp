/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainBPFL.cpp
 * @brief   Implementation of terrain query Blueprint nodes.
 */

#include "Lib/SeinTerrainBPFL.h"
#include "SeinNavigationGrid.h"

FGameplayTag USeinTerrainBPFL::SeinGetTerrainTypeAt(USeinNavigationGrid* Grid, FFixedVector Location)
{
	if (!Grid) return FGameplayTag();

	FIntPoint GridPos = Grid->WorldToGrid(Location);
	if (!Grid->IsValidCell(GridPos)) return FGameplayTag();

	return Grid->GetTerrainType(GridPos);
}

uint8 USeinTerrainBPFL::SeinGetMovementCostAt(USeinNavigationGrid* Grid, FFixedVector Location)
{
	if (!Grid) return 0;

	FIntPoint GridPos = Grid->WorldToGrid(Location);
	if (!Grid->IsValidCell(GridPos)) return 0;

	return Grid->GetMovementCost(GridPos);
}

bool USeinTerrainBPFL::SeinIsLocationWalkable(USeinNavigationGrid* Grid, FFixedVector Location)
{
	if (!Grid) return false;

	FIntPoint GridPos = Grid->WorldToGrid(Location);
	if (!Grid->IsValidCell(GridPos)) return false;

	return Grid->IsWalkable(GridPos);
}

FFixedVector USeinTerrainBPFL::SeinGetNearestWalkableLocation(USeinNavigationGrid* Grid, FFixedVector Location, FFixedPoint SearchRadius)
{
	if (!Grid) return Location;

	FIntPoint CenterGrid = Grid->WorldToGrid(Location);
	if (Grid->IsValidCell(CenterGrid) && Grid->IsWalkable(CenterGrid))
	{
		return Location;
	}

	// Spiral search outward
	FFixedPoint BestDistSq = SearchRadius * SearchRadius + FFixedPoint::One;
	FFixedVector BestLocation = Location;
	bool bFound = false;

	int32 SearchCells = (SearchRadius / Grid->CellSize).ToInt() + 1;

	for (int32 DX = -SearchCells; DX <= SearchCells; ++DX)
	{
		for (int32 DY = -SearchCells; DY <= SearchCells; ++DY)
		{
			FIntPoint TestPos(CenterGrid.X + DX, CenterGrid.Y + DY);
			if (!Grid->IsValidCell(TestPos) || !Grid->IsWalkable(TestPos)) continue;

			FFixedVector WorldPos = Grid->GridToWorldCenter(TestPos);
			FFixedVector Delta = WorldPos - Location;
			FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestLocation = WorldPos;
				bFound = true;
			}
		}
	}

	return BestLocation;
}

FIntPoint USeinTerrainBPFL::SeinWorldToGrid(USeinNavigationGrid* Grid, FFixedVector WorldLocation)
{
	if (!Grid) return FIntPoint(-1, -1);
	return Grid->WorldToGrid(WorldLocation);
}

FFixedVector USeinTerrainBPFL::SeinGridToWorld(USeinNavigationGrid* Grid, FIntPoint GridPosition)
{
	if (!Grid) return FFixedVector::ZeroVector;
	return Grid->GridToWorldCenter(GridPosition);
}
