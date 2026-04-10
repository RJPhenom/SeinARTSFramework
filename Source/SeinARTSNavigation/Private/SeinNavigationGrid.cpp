#include "SeinNavigationGrid.h"

void USeinNavigationGrid::InitializeGrid(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin)
{
	GridWidth = Width;
	GridHeight = Height;
	CellSize = InCellSize;
	GridOrigin = Origin;

	const int32 TotalCells = Width * Height;

	// All cells walkable with cost 1 by default
	Walkability.SetNumZeroed(TotalCells);
	for (int32 i = 0; i < TotalCells; ++i)
	{
		Walkability[i] = 1;
	}

	TerrainTypes.SetNum(TotalCells);

	DynamicBlocked.Init(false, TotalCells);
}

FIntPoint USeinNavigationGrid::WorldToGrid(const FFixedVector& WorldPos) const
{
	// Subtract origin, divide by cell size, floor to integer
	const FFixedPoint RelX = WorldPos.X - GridOrigin.X;
	const FFixedPoint RelY = WorldPos.Y - GridOrigin.Y;

	const int32 CellX = (RelX / CellSize).ToInt();
	const int32 CellY = (RelY / CellSize).ToInt();

	return FIntPoint(CellX, CellY);
}

FFixedVector USeinNavigationGrid::GridToWorld(FIntPoint GridPos) const
{
	// Cell origin (bottom-left corner) in world space
	const FFixedPoint WorldX = GridOrigin.X + FFixedPoint::FromInt(GridPos.X) * CellSize;
	const FFixedPoint WorldY = GridOrigin.Y + FFixedPoint::FromInt(GridPos.Y) * CellSize;
	return FFixedVector(WorldX, WorldY, GridOrigin.Z);
}

FFixedVector USeinNavigationGrid::GridToWorldCenter(FIntPoint GridPos) const
{
	const FFixedPoint HalfCell = CellSize * FFixedPoint::Half;
	const FFixedPoint WorldX = GridOrigin.X + FFixedPoint::FromInt(GridPos.X) * CellSize + HalfCell;
	const FFixedPoint WorldY = GridOrigin.Y + FFixedPoint::FromInt(GridPos.Y) * CellSize + HalfCell;
	return FFixedVector(WorldX, WorldY, GridOrigin.Z);
}

bool USeinNavigationGrid::IsValidCell(FIntPoint GridPos) const
{
	return GridPos.X >= 0 && GridPos.X < GridWidth
		&& GridPos.Y >= 0 && GridPos.Y < GridHeight;
}

bool USeinNavigationGrid::IsWalkable(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos))
	{
		return false;
	}

	const int32 Index = CellIndex(GridPos);
	return Walkability[Index] > 0 && !DynamicBlocked[Index];
}

uint8 USeinNavigationGrid::GetMovementCost(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos))
	{
		return 0;
	}
	return Walkability[CellIndex(GridPos)];
}

FGameplayTag USeinNavigationGrid::GetTerrainType(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos))
	{
		return FGameplayTag();
	}
	return TerrainTypes[CellIndex(GridPos)];
}

void USeinNavigationGrid::MarkCellBlocked(FIntPoint GridPos)
{
	if (IsValidCell(GridPos))
	{
		DynamicBlocked[CellIndex(GridPos)] = true;
	}
}

void USeinNavigationGrid::MarkCellUnblocked(FIntPoint GridPos)
{
	if (IsValidCell(GridPos))
	{
		DynamicBlocked[CellIndex(GridPos)] = false;
	}
}

void USeinNavigationGrid::MarkAreaBlocked(FIntPoint Min, FIntPoint Max)
{
	const int32 MinX = FMath::Max(Min.X, 0);
	const int32 MinY = FMath::Max(Min.Y, 0);
	const int32 MaxX = FMath::Min(Max.X, GridWidth - 1);
	const int32 MaxY = FMath::Min(Max.Y, GridHeight - 1);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			DynamicBlocked[CellIndex(FIntPoint(X, Y))] = true;
		}
	}
}

void USeinNavigationGrid::MarkAreaUnblocked(FIntPoint Min, FIntPoint Max)
{
	const int32 MinX = FMath::Max(Min.X, 0);
	const int32 MinY = FMath::Max(Min.Y, 0);
	const int32 MaxX = FMath::Min(Max.X, GridWidth - 1);
	const int32 MaxY = FMath::Min(Max.Y, GridHeight - 1);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			DynamicBlocked[CellIndex(FIntPoint(X, Y))] = false;
		}
	}
}

int32 USeinNavigationGrid::CellIndex(FIntPoint GridPos) const
{
	return GridPos.Y * GridWidth + GridPos.X;
}

bool USeinNavigationGrid::IsValidIndex(int32 Index) const
{
	return Index >= 0 && Index < GridWidth * GridHeight;
}

TArray<FIntPoint> USeinNavigationGrid::GetNeighbors(FIntPoint GridPos, bool bIncludeDiagonals) const
{
	TArray<FIntPoint> Neighbors;
	Neighbors.Reserve(bIncludeDiagonals ? 8 : 4);

	// Cardinal directions (N, E, S, W)
	static const FIntPoint CardinalOffsets[] = {
		FIntPoint( 0,  1),  // North
		FIntPoint( 1,  0),  // East
		FIntPoint( 0, -1),  // South
		FIntPoint(-1,  0),  // West
	};

	// Diagonal directions (NE, SE, SW, NW)
	static const FIntPoint DiagonalOffsets[] = {
		FIntPoint( 1,  1),  // NE
		FIntPoint( 1, -1),  // SE
		FIntPoint(-1, -1),  // SW
		FIntPoint(-1,  1),  // NW
	};

	for (const FIntPoint& Offset : CardinalOffsets)
	{
		const FIntPoint Neighbor(GridPos.X + Offset.X, GridPos.Y + Offset.Y);
		if (IsValidCell(Neighbor))
		{
			Neighbors.Add(Neighbor);
		}
	}

	if (bIncludeDiagonals)
	{
		for (const FIntPoint& Offset : DiagonalOffsets)
		{
			const FIntPoint Neighbor(GridPos.X + Offset.X, GridPos.Y + Offset.Y);
			if (IsValidCell(Neighbor))
			{
				Neighbors.Add(Neighbor);
			}
		}
	}

	return Neighbors;
}
