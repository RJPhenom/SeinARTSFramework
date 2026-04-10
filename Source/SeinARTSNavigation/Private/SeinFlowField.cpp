#include "SeinFlowField.h"

// 8 compass directions indexed 0-7:
// 0=E, 1=NE, 2=N, 3=NW, 4=W, 5=SW, 6=S, 7=SE
// Using fixed-point unit vectors (and diagonal vectors normalized to ~0.707)
static const FIntPoint DirectionOffsets[8] = {
	FIntPoint( 1,  0),  // 0: East
	FIntPoint( 1,  1),  // 1: NE
	FIntPoint( 0,  1),  // 2: North
	FIntPoint(-1,  1),  // 3: NW
	FIntPoint(-1,  0),  // 4: West
	FIntPoint(-1, -1),  // 5: SW
	FIntPoint( 0, -1),  // 6: South
	FIntPoint( 1, -1),  // 7: SE
};

// Movement costs: cardinal = 10, diagonal = 14
static constexpr int32 MOVE_COST_CARDINAL = 10;
static constexpr int32 MOVE_COST_DIAGONAL = 14;

static int32 GetDirectionMoveCost(int32 DirIndex)
{
	// Odd indices are diagonals (NE, NW, SW, SE)
	return (DirIndex & 1) ? MOVE_COST_DIAGONAL : MOVE_COST_CARDINAL;
}

// ----------------------------------------------------------------
// FSeinFlowField
// ----------------------------------------------------------------

FFixedVector FSeinFlowField::GetFlowDirection(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos))
	{
		return FFixedVector();
	}

	const int32 Index = CellIndex(GridPos);
	const uint8 Dir = Directions[Index];

	if (Dir == DIR_UNREACHABLE || Dir > 7)
	{
		return FFixedVector();
	}

	const FIntPoint& Offset = DirectionOffsets[Dir];

	// For cardinal directions, return unit vector.
	// For diagonal directions, return normalized vector (InvSqrt2 per component).
	const bool bDiagonal = (Dir & 1) != 0;

	if (bDiagonal)
	{
		// Diagonal: each component is +/- 1/sqrt(2) ~= 0.70710678
		const FFixedPoint Component = FFixedPoint::InvSqrt2;
		return FFixedVector(
			Offset.X > 0 ? Component : (Offset.X < 0 ? -Component : FFixedPoint::Zero),
			Offset.Y > 0 ? Component : (Offset.Y < 0 ? -Component : FFixedPoint::Zero),
			FFixedPoint::Zero
		);
	}
	else
	{
		// Cardinal: one component is +/- 1, the other is 0
		return FFixedVector(
			FFixedPoint::FromInt(Offset.X),
			FFixedPoint::FromInt(Offset.Y),
			FFixedPoint::Zero
		);
	}
}

bool FSeinFlowField::IsReachable(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos))
	{
		return false;
	}
	return Directions[CellIndex(GridPos)] != DIR_UNREACHABLE;
}

// ----------------------------------------------------------------
// USeinFlowFieldGenerator
// ----------------------------------------------------------------

void USeinFlowFieldGenerator::Initialize(USeinNavigationGrid* InGrid)
{
	Grid = InGrid;
}

FSeinFlowField USeinFlowFieldGenerator::GenerateFlowField(FIntPoint Destination)
{
	FSeinFlowField Field;

	if (!Grid || !Grid->IsValidCell(Destination))
	{
		return Field;
	}

	Field.Width = Grid->GridWidth;
	Field.Height = Grid->GridHeight;
	Field.Destination = Destination;

	const int32 TotalCells = Field.Width * Field.Height;

	// Initialize cost field to unreachable
	Field.CostField.SetNumUninitialized(TotalCells);
	for (int32 i = 0; i < TotalCells; ++i)
	{
		Field.CostField[i] = FSeinFlowField::COST_UNREACHABLE;
	}

	// Initialize direction field to unreachable
	Field.Directions.SetNumUninitialized(TotalCells);
	FMemory::Memset(Field.Directions.GetData(), FSeinFlowField::DIR_UNREACHABLE, TotalCells);

	// ---------------------------------------------------------------
	// Phase 1: Dijkstra integration (BFS with costs) from destination
	// ---------------------------------------------------------------

	// Open list as a FIFO queue for BFS (with costs, so we use a sorted insertion)
	// For simplicity and determinism, use a TArray as a priority queue
	struct FOpenNode
	{
		FIntPoint Pos;
		int32 Cost;
	};

	TArray<FOpenNode> OpenList;
	OpenList.Reserve(TotalCells / 4);

	// Seed destination with cost 0
	const int32 DestIndex = Grid->CellIndex(Destination);
	Field.CostField[DestIndex] = 0;

	FOpenNode DestNode;
	DestNode.Pos = Destination;
	DestNode.Cost = 0;
	OpenList.Add(DestNode);

	while (OpenList.Num() > 0)
	{
		// Find minimum cost node (simple linear scan for determinism)
		int32 BestIdx = 0;
		for (int32 i = 1; i < OpenList.Num(); ++i)
		{
			if (OpenList[i].Cost < OpenList[BestIdx].Cost)
			{
				BestIdx = i;
			}
		}

		const FOpenNode Current = OpenList[BestIdx];
		OpenList.RemoveAtSwap(BestIdx);

		const int32 CurrentIndex = Grid->CellIndex(Current.Pos);

		// Skip if we already found a better path
		if (Current.Cost > Field.CostField[CurrentIndex])
		{
			continue;
		}

		// Expand all 8 neighbors
		for (int32 DirIdx = 0; DirIdx < 8; ++DirIdx)
		{
			const FIntPoint& Offset = DirectionOffsets[DirIdx];
			const FIntPoint NeighborPos(Current.Pos.X + Offset.X, Current.Pos.Y + Offset.Y);

			if (!Grid->IsValidCell(NeighborPos) || !Grid->IsWalkable(NeighborPos))
			{
				continue;
			}

			// For diagonal moves, ensure both cardinal neighbors are walkable (no corner cutting)
			const bool bDiagonal = (DirIdx & 1) != 0;
			if (bDiagonal)
			{
				const FIntPoint Adj1(Current.Pos.X + Offset.X, Current.Pos.Y);
				const FIntPoint Adj2(Current.Pos.X, Current.Pos.Y + Offset.Y);
				if (!Grid->IsWalkable(Adj1) || !Grid->IsWalkable(Adj2))
				{
					continue;
				}
			}

			const int32 MoveCost = GetDirectionMoveCost(DirIdx);
			const int32 CellCost = Grid->GetMovementCost(NeighborPos);
			const int32 NewCost = Current.Cost + MoveCost * CellCost;

			const int32 NeighborIndex = Grid->CellIndex(NeighborPos);

			if (NewCost < Field.CostField[NeighborIndex])
			{
				Field.CostField[NeighborIndex] = NewCost;

				FOpenNode NewNode;
				NewNode.Pos = NeighborPos;
				NewNode.Cost = NewCost;
				OpenList.Add(NewNode);
			}
		}
	}

	// ---------------------------------------------------------------
	// Phase 2: Compute direction field
	// Each cell points toward its lowest-cost neighbor
	// ---------------------------------------------------------------

	for (int32 Y = 0; Y < Field.Height; ++Y)
	{
		for (int32 X = 0; X < Field.Width; ++X)
		{
			const FIntPoint CellPos(X, Y);
			const int32 Index = Grid->CellIndex(CellPos);

			if (Field.CostField[Index] == FSeinFlowField::COST_UNREACHABLE)
			{
				continue; // Unreachable cells stay DIR_UNREACHABLE
			}

			// Destination cell has no direction (we are already there)
			if (CellPos == Destination)
			{
				Field.Directions[Index] = FSeinFlowField::DIR_UNREACHABLE;
				continue;
			}

			int32 BestCost = Field.CostField[Index];
			uint8 BestDir = FSeinFlowField::DIR_UNREACHABLE;

			for (int32 DirIdx = 0; DirIdx < 8; ++DirIdx)
			{
				const FIntPoint& Offset = DirectionOffsets[DirIdx];
				const FIntPoint NeighborPos(X + Offset.X, Y + Offset.Y);

				if (!Grid->IsValidCell(NeighborPos))
				{
					continue;
				}

				const int32 NeighborIndex = Grid->CellIndex(NeighborPos);
				if (Field.CostField[NeighborIndex] < BestCost)
				{
					BestCost = Field.CostField[NeighborIndex];
					BestDir = static_cast<uint8>(DirIdx);
				}
			}

			Field.Directions[Index] = BestDir;
		}
	}

	return Field;
}
