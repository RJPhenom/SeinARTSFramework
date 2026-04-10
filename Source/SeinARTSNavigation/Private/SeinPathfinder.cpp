#include "SeinPathfinder.h"

// Movement cost constants (integer-scaled for determinism)
// Straight movement cost = 10, diagonal ~= 10 * sqrt(2) ~= 14
static constexpr int32 COST_STRAIGHT = 10;
static constexpr int32 COST_DIAGONAL = 14;

// Maximum number of nodes to evaluate before giving up (prevents infinite loops on huge grids)
static constexpr int32 MAX_SEARCH_NODES = 65536;

void USeinPathfinder::Initialize(USeinNavigationGrid* InGrid)
{
	Grid = InGrid;
}

bool USeinPathfinder::IsCellPassable(FIntPoint GridPos, const FSeinPathRequest& Request) const
{
	if (!Grid->IsWalkable(GridPos))
	{
		return false;
	}

	const uint8 Cost = Grid->GetMovementCost(GridPos);
	if (Cost > Request.MaxMovementCost)
	{
		return false;
	}

	// Check terrain tag restrictions
	if (Request.BlockedTerrainTags.Num() > 0)
	{
		const FGameplayTag CellTerrain = Grid->GetTerrainType(GridPos);
		if (CellTerrain.IsValid() && Request.BlockedTerrainTags.HasTag(CellTerrain))
		{
			return false;
		}
	}

	return true;
}

int32 USeinPathfinder::HeuristicCost(FIntPoint A, FIntPoint B) const
{
	// Octile distance: max(dx,dy)*10 + min(dx,dy)*4
	// The *4 comes from (14-10) = 4, the extra cost of going diagonal vs straight
	const int32 DX = FMath::Abs(A.X - B.X);
	const int32 DY = FMath::Abs(A.Y - B.Y);
	const int32 MinD = FMath::Min(DX, DY);
	const int32 MaxD = FMath::Max(DX, DY);
	return MaxD * COST_STRAIGHT + MinD * (COST_DIAGONAL - COST_STRAIGHT);
}

FSeinPath USeinPathfinder::FindPath(const FSeinPathRequest& Request)
{
	FSeinPath Result;
	Result.bIsValid = false;

	if (!Grid)
	{
		return Result;
	}

	const FIntPoint StartCell = Grid->WorldToGrid(Request.Start);
	const FIntPoint EndCell = Grid->WorldToGrid(Request.End);

	// Validate start and end
	if (!Grid->IsValidCell(StartCell) || !Grid->IsValidCell(EndCell))
	{
		return Result;
	}

	if (!IsCellPassable(StartCell, Request))
	{
		return Result;
	}

	// Trivial case: start == end
	if (StartCell == EndCell)
	{
		Result.Waypoints.Add(Grid->GridToWorldCenter(StartCell));
		Result.TotalCost = FFixedPoint::Zero;
		Result.bIsValid = true;
		return Result;
	}

	// If destination is blocked, we will return a partial path to the closest reachable cell
	const bool bDestinationBlocked = !IsCellPassable(EndCell, Request);

	// A* data structures
	TMap<int32, FPathNode> AllNodes;
	AllNodes.Reserve(1024);

	// Open set as a sorted array (priority queue by FCost, then HCost for tie-breaking)
	TArray<int32> OpenList; // Indices into grid
	OpenList.Reserve(256);

	// Closed set
	TSet<int32> ClosedSet;
	ClosedSet.Reserve(1024);

	// Initialize start node
	const int32 StartIndex = Grid->CellIndex(StartCell);
	FPathNode StartNode;
	StartNode.Position = StartCell;
	StartNode.GCost = 0;
	StartNode.HCost = HeuristicCost(StartCell, EndCell);
	StartNode.bHasParent = false;
	AllNodes.Add(StartIndex, StartNode);
	OpenList.Add(StartIndex);

	// Track best partial path candidate (closest node to destination)
	int32 BestPartialIndex = StartIndex;
	int32 BestPartialHCost = StartNode.HCost;

	int32 NodesEvaluated = 0;

	while (OpenList.Num() > 0 && NodesEvaluated < MAX_SEARCH_NODES)
	{
		// Find node with lowest FCost (ties broken by lowest HCost)
		int32 BestOpenIdx = 0;
		int32 BestFCost = AllNodes[OpenList[0]].FCost();
		int32 BestHCost = AllNodes[OpenList[0]].HCost;

		for (int32 i = 1; i < OpenList.Num(); ++i)
		{
			const FPathNode& Node = AllNodes[OpenList[i]];
			const int32 NodeFCost = Node.FCost();
			if (NodeFCost < BestFCost || (NodeFCost == BestFCost && Node.HCost < BestHCost))
			{
				BestOpenIdx = i;
				BestFCost = NodeFCost;
				BestHCost = Node.HCost;
			}
		}

		const int32 CurrentIndex = OpenList[BestOpenIdx];
		OpenList.RemoveAtSwap(BestOpenIdx);

		const FPathNode& CurrentNode = AllNodes[CurrentIndex];
		const FIntPoint CurrentPos = CurrentNode.Position;

		// Check if we reached the destination
		if (CurrentPos == EndCell)
		{
			Result = ReconstructPath(AllNodes, StartCell, EndCell, Request);
			Result.bIsValid = true;
			Result.bIsPartial = false;
			return Result;
		}

		ClosedSet.Add(CurrentIndex);
		++NodesEvaluated;

		// Track best partial candidate
		if (CurrentNode.HCost < BestPartialHCost)
		{
			BestPartialHCost = CurrentNode.HCost;
			BestPartialIndex = CurrentIndex;
		}

		// Expand neighbors (8-connectivity)
		const TArray<FIntPoint> Neighbors = Grid->GetNeighbors(CurrentPos, true);

		for (const FIntPoint& NeighborPos : Neighbors)
		{
			const int32 NeighborIndex = Grid->CellIndex(NeighborPos);

			if (ClosedSet.Contains(NeighborIndex))
			{
				continue;
			}

			if (!IsCellPassable(NeighborPos, Request))
			{
				continue;
			}

			// Determine if this is a diagonal move
			const int32 DX = FMath::Abs(NeighborPos.X - CurrentPos.X);
			const int32 DY = FMath::Abs(NeighborPos.Y - CurrentPos.Y);
			const bool bDiagonal = (DX + DY == 2);

			// For diagonal moves, check that both adjacent cardinal cells are walkable
			// to prevent cutting corners through walls
			if (bDiagonal)
			{
				const FIntPoint Adjacent1(CurrentPos.X + (NeighborPos.X - CurrentPos.X), CurrentPos.Y);
				const FIntPoint Adjacent2(CurrentPos.X, CurrentPos.Y + (NeighborPos.Y - CurrentPos.Y));
				if (!IsCellPassable(Adjacent1, Request) || !IsCellPassable(Adjacent2, Request))
				{
					continue;
				}
			}

			const int32 MoveCost = bDiagonal ? COST_DIAGONAL : COST_STRAIGHT;
			const int32 CellCost = Grid->GetMovementCost(NeighborPos);
			const int32 TentativeG = CurrentNode.GCost + MoveCost * CellCost;

			FPathNode* ExistingNode = AllNodes.Find(NeighborIndex);
			if (ExistingNode != nullptr)
			{
				// Already in open set - check if this path is better
				if (TentativeG < ExistingNode->GCost)
				{
					ExistingNode->GCost = TentativeG;
					ExistingNode->Parent = CurrentPos;
					ExistingNode->bHasParent = true;
				}
			}
			else
			{
				// New node
				FPathNode NewNode;
				NewNode.Position = NeighborPos;
				NewNode.GCost = TentativeG;
				NewNode.HCost = HeuristicCost(NeighborPos, EndCell);
				NewNode.Parent = CurrentPos;
				NewNode.bHasParent = true;
				AllNodes.Add(NeighborIndex, NewNode);
				OpenList.Add(NeighborIndex);
			}
		}
	}

	// No complete path found - return partial path to closest reachable cell
	if (BestPartialIndex != StartIndex)
	{
		const FPathNode& BestPartialNode = AllNodes[BestPartialIndex];
		Result = ReconstructPath(AllNodes, StartCell, BestPartialNode.Position, Request);
		Result.bIsValid = true;
		Result.bIsPartial = true;
	}

	return Result;
}

FSeinPath USeinPathfinder::ReconstructPath(const TMap<int32, FPathNode>& NodeMap, FIntPoint Start, FIntPoint End, const FSeinPathRequest& Request)
{
	FSeinPath RawPath;
	RawPath.bIsValid = true;

	// Trace backwards from end to start
	TArray<FIntPoint> GridPath;
	FIntPoint Current = End;
	int32 SafetyCounter = MAX_SEARCH_NODES;

	while (Current != Start && SafetyCounter > 0)
	{
		GridPath.Add(Current);

		const int32 Index = Grid->CellIndex(Current);
		const FPathNode* Node = NodeMap.Find(Index);
		if (!Node || !Node->bHasParent)
		{
			break;
		}
		Current = Node->Parent;
		--SafetyCounter;
	}
	GridPath.Add(Start);

	// Reverse to get start-to-end order
	Algo::Reverse(GridPath);

	// Convert grid positions to world-space waypoints
	FFixedPoint TotalCost = FFixedPoint::Zero;
	for (int32 i = 0; i < GridPath.Num(); ++i)
	{
		RawPath.Waypoints.Add(Grid->GridToWorldCenter(GridPath[i]));

		if (i > 0)
		{
			const int32 DX = FMath::Abs(GridPath[i].X - GridPath[i - 1].X);
			const int32 DY = FMath::Abs(GridPath[i].Y - GridPath[i - 1].Y);
			const bool bDiag = (DX + DY == 2);
			const int32 StepCost = bDiag ? COST_DIAGONAL : COST_STRAIGHT;
			TotalCost += FFixedPoint::FromInt(StepCost * Grid->GetMovementCost(GridPath[i]));
		}
	}
	RawPath.TotalCost = TotalCost;

	return SmoothPath(RawPath);
}

FSeinPath USeinPathfinder::SmoothPath(const FSeinPath& RawPath)
{
	if (RawPath.Waypoints.Num() <= 2)
	{
		return RawPath;
	}

	FSeinPath Smoothed;
	Smoothed.bIsValid = RawPath.bIsValid;
	Smoothed.bIsPartial = RawPath.bIsPartial;
	Smoothed.TotalCost = RawPath.TotalCost;

	// Keep first waypoint
	Smoothed.Waypoints.Add(RawPath.Waypoints[0]);

	// Remove collinear intermediate points:
	// Three consecutive points A, B, C are collinear if the direction A->B == B->C
	for (int32 i = 1; i < RawPath.Waypoints.Num() - 1; ++i)
	{
		const FFixedVector& Prev = RawPath.Waypoints[i - 1];
		const FFixedVector& Curr = RawPath.Waypoints[i];
		const FFixedVector& Next = RawPath.Waypoints[i + 1];

		// Check collinearity by comparing direction vectors
		// Direction from prev to curr
		const FFixedPoint DX1 = Curr.X - Prev.X;
		const FFixedPoint DY1 = Curr.Y - Prev.Y;

		// Direction from curr to next
		const FFixedPoint DX2 = Next.X - Curr.X;
		const FFixedPoint DY2 = Next.Y - Curr.Y;

		// Cross product of 2D direction vectors: if zero, points are collinear
		const FFixedPoint Cross = DX1 * DY2 - DY1 * DX2;

		if (Cross != FFixedPoint::Zero)
		{
			// Not collinear - keep this waypoint
			Smoothed.Waypoints.Add(Curr);
		}
	}

	// Keep last waypoint
	Smoothed.Waypoints.Add(RawPath.Waypoints.Last());

	return Smoothed;
}

int32 USeinPathfinder::ProcessBatch(TArray<FSeinPathRequest>& Requests, TArray<FSeinPath>& OutPaths, int32 MaxPaths)
{
	const int32 Count = FMath::Min(Requests.Num(), MaxPaths);
	OutPaths.Reserve(OutPaths.Num() + Count);

	for (int32 i = 0; i < Count; ++i)
	{
		OutPaths.Add(FindPath(Requests[i]));
	}

	// Remove processed requests from the front
	if (Count > 0)
	{
		Requests.RemoveAt(0, Count);
	}

	return Count;
}
