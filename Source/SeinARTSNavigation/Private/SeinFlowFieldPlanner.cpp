#include "SeinFlowFieldPlanner.h"

#include "SeinNavigationGrid.h"
#include "Grid/SeinAbstractGraph.h"
#include "Grid/SeinNavigationLayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFlowPlan, Log, All);

void USeinFlowFieldPlanner::Initialize(USeinNavigationGrid* InGrid)
{
	Grid = InGrid;
	PlanCache.Reset();
}

FGuid USeinFlowFieldPlanner::KeyForCells(const FSeinCellAddress& Start, const FSeinCellAddress& Goal)
{
	// Deterministic GUID from (layer, index) tuples — reasonable for single-unit caches.
	uint32 A = HashCombine(::GetTypeHash(Start.Layer), ::GetTypeHash(Start.CellIndex));
	uint32 B = HashCombine(::GetTypeHash(Goal.Layer), ::GetTypeHash(Goal.CellIndex));
	return FGuid(A, B, 0xA11C0F10u, 0x9F9F9F9Fu);
}

FSeinFlowFieldPlan& USeinFlowFieldPlanner::GetOrBuild(
	const FGuid& Key,
	const FSeinCellAddress& Start,
	const FSeinCellAddress& Goal,
	const FGameplayTagContainer& AgentTags)
{
	if (FSeinFlowFieldPlan* Existing = PlanCache.Find(Key))
	{
		if (Existing->bValid && Existing->Start == Start && Existing->Goal == Goal)
		{
			return *Existing;
		}
	}
	FSeinFlowFieldPlan Built = BuildPlan(Start, Goal, AgentTags);
	Built.PlanID = Key;
	return PlanCache.Add(Key, MoveTemp(Built));
}

void USeinFlowFieldPlanner::InvalidatePlan(const FGuid& Key)
{
	PlanCache.Remove(Key);
}

void USeinFlowFieldPlanner::InvalidateAll()
{
	PlanCache.Reset();
}

FSeinFlowFieldPlan USeinFlowFieldPlanner::BuildPlan(
	const FSeinCellAddress& Start,
	const FSeinCellAddress& Goal,
	const FGameplayTagContainer& AgentTags)
{
	FSeinFlowFieldPlan Plan;
	Plan.Start = Start;
	Plan.Goal = Goal;
	Plan.AgentTags = AgentTags;
	Plan.bValid = false;

	if (!Grid || !Grid->IsValidCellAddr(Start) || !Grid->IsValidCellAddr(Goal))
	{
		return Plan;
	}
	if (!Grid->IsWalkableAddr(Start) || !Grid->IsWalkableAddr(Goal))
	{
		return Plan;
	}

	const int32 StartCluster = Grid->ClusterIDForCell(Start);
	const int32 GoalCluster = Grid->ClusterIDForCell(Goal);

	if (StartCluster == INDEX_NONE || GoalCluster == INDEX_NONE)
	{
		return Plan;
	}

	// Single-cluster short-circuit.
	if (Start.Layer == Goal.Layer && StartCluster == GoalCluster)
	{
		FSeinAbstractPathStep Step;
		Step.Layer = Goal.Layer;
		Step.ClusterID = GoalCluster;
		Step.GoalCellIndex = Goal.CellIndex;
		Step.EdgeIndex = INDEX_NONE;
		Step.NavLinkID = INDEX_NONE;
		Step.FlowFieldIndex = 0;
		Plan.Steps.Add(Step);

		FSeinClusterFlowField Field;
		ComputeClusterField(Step, Field);
		Plan.PerClusterFields.Add(MoveTemp(Field));
		Plan.bValid = true;
		return Plan;
	}

	// Route on the abstract graph.
	const int32 StartNodeIdx = Grid->AbstractGraph.FindNode(Start.Layer, StartCluster);
	const int32 GoalNodeIdx = Grid->AbstractGraph.FindNode(Goal.Layer, GoalCluster);
	if (StartNodeIdx == INDEX_NONE || GoalNodeIdx == INDEX_NONE)
	{
		return Plan;
	}

	TArray<int32> NodeSequence;
	TArray<int32> EdgeSequence;
	if (!RouteAbstract(StartNodeIdx, GoalNodeIdx, AgentTags, NodeSequence, EdgeSequence))
	{
		return Plan;
	}

	// Build steps. Each node in the sequence = one step; outgoing edge (if any) ends it.
	Plan.Steps.Reserve(NodeSequence.Num());
	for (int32 i = 0; i < NodeSequence.Num(); ++i)
	{
		const FSeinAbstractNode& Node = Grid->AbstractGraph.Nodes[NodeSequence[i]];
		FSeinAbstractPathStep Step;
		Step.Layer = Node.Layer;
		Step.ClusterID = Node.ClusterID;
		Step.EdgeIndex = EdgeSequence.IsValidIndex(i) ? EdgeSequence[i] : INDEX_NONE;
		if (Step.EdgeIndex != INDEX_NONE && Grid->AbstractGraph.Edges.IsValidIndex(Step.EdgeIndex))
		{
			const FSeinAbstractEdge& Edge = Grid->AbstractGraph.Edges[Step.EdgeIndex];
			Step.NavLinkID = (Edge.EdgeType == ESeinAbstractEdgeType::NavLink) ? Edge.NavLinkID : INDEX_NONE;
		}

		// GoalCellIndex: final step uses the goal; earlier steps aim at the representative cell of the NEXT node
		// (which sits inside this cluster's border if the next cluster is adjacent, or at the navlink endpoint).
		if (i + 1 < NodeSequence.Num())
		{
			const FSeinAbstractNode& NextNode = Grid->AbstractGraph.Nodes[NodeSequence[i + 1]];
			// If the outgoing edge is a navlink, aim at the navlink start endpoint (this cluster's side).
			if (Step.NavLinkID != INDEX_NONE && Grid->NavLinks.IsValidIndex(Step.NavLinkID))
			{
				const FSeinNavLinkRecord& Link = Grid->NavLinks[Step.NavLinkID];
				const bool bStartIsThisCluster = (Link.StartCell.Layer == Step.Layer);
				Step.GoalCellIndex = bStartIsThisCluster ? Link.StartCell.CellIndex : Link.EndCell.CellIndex;
			}
			else
			{
				// Cluster-border edge — aim at the neighbor cluster's representative cell (approximation).
				Step.GoalCellIndex = NextNode.RepresentativeCellIndex;
			}
		}
		else
		{
			Step.GoalCellIndex = Goal.CellIndex;
		}

		Plan.Steps.Add(Step);
	}

	// Compute flow fields for each step's cluster.
	Plan.PerClusterFields.Reserve(Plan.Steps.Num());
	for (int32 i = 0; i < Plan.Steps.Num(); ++i)
	{
		FSeinAbstractPathStep& Step = Plan.Steps[i];
		FSeinClusterFlowField Field;
		ComputeClusterField(Step, Field);
		Step.FlowFieldIndex = Plan.PerClusterFields.Add(MoveTemp(Field));
	}

	Plan.bValid = true;
	return Plan;
}

bool USeinFlowFieldPlanner::RouteAbstract(
	int32 StartNodeIdx,
	int32 GoalNodeIdx,
	const FGameplayTagContainer& AgentTags,
	TArray<int32>& OutNodeSequence,
	TArray<int32>& OutEdgeSequence) const
{
	if (!Grid) { return false; }
	const FSeinAbstractGraph& G = Grid->AbstractGraph;
	if (!G.Nodes.IsValidIndex(StartNodeIdx) || !G.Nodes.IsValidIndex(GoalNodeIdx))
	{
		return false;
	}

	// Dijkstra / uniform A* (heuristic=0) — sufficient given our small abstract-graph sizes.
	struct FOpen
	{
		int32 Node;
		int32 GCost;
		bool operator<(const FOpen& Other) const { return GCost > Other.GCost; } // min-heap via std::less<>
	};

	TArray<int32> GCost;
	TArray<int32> Parent;
	TArray<int32> ParentEdge;
	GCost.Init(MAX_int32, G.Nodes.Num());
	Parent.Init(INDEX_NONE, G.Nodes.Num());
	ParentEdge.Init(INDEX_NONE, G.Nodes.Num());

	TArray<FOpen> Heap;
	Heap.HeapPush({ StartNodeIdx, 0 }, TLess<FOpen>());
	GCost[StartNodeIdx] = 0;

	while (Heap.Num() > 0)
	{
		FOpen Cur;
		Heap.HeapPop(Cur, TLess<FOpen>(), EAllowShrinking::No);

		if (Cur.Node == GoalNodeIdx) { break; }
		if (Cur.GCost > GCost[Cur.Node]) { continue; }

		const FSeinAbstractNode& CurNode = G.Nodes[Cur.Node];
		for (int32 EdgeIdx : CurNode.OutgoingEdgeIndices)
		{
			if (!G.Edges.IsValidIndex(EdgeIdx)) { continue; }
			const FSeinAbstractEdge& E = G.Edges[EdgeIdx];
			if (!E.bEnabled) { continue; }
			if (E.EdgeType == ESeinAbstractEdgeType::NavLink && !E.EligibilityQuery.IsEmpty())
			{
				if (!E.EligibilityQuery.Matches(AgentTags))
				{
					continue;
				}
			}

			const int32 NewG = Cur.GCost + FMath::Max(1, E.Cost);
			if (NewG < GCost[E.ToNode])
			{
				GCost[E.ToNode] = NewG;
				Parent[E.ToNode] = Cur.Node;
				ParentEdge[E.ToNode] = EdgeIdx;
				Heap.HeapPush({ E.ToNode, NewG }, TLess<FOpen>());
			}
		}
	}

	if (GCost[GoalNodeIdx] == MAX_int32) { return false; }

	// Reconstruct.
	TArray<int32> NodeStack;
	TArray<int32> EdgeStack;
	int32 Cur = GoalNodeIdx;
	NodeStack.Add(Cur);
	while (Parent[Cur] != INDEX_NONE)
	{
		EdgeStack.Add(ParentEdge[Cur]);
		Cur = Parent[Cur];
		NodeStack.Add(Cur);
	}
	Algo::Reverse(NodeStack);
	Algo::Reverse(EdgeStack);

	OutNodeSequence = MoveTemp(NodeStack);
	OutEdgeSequence = MoveTemp(EdgeStack);
	return true;
}

void USeinFlowFieldPlanner::ComputeClusterField(
	const FSeinAbstractPathStep& Step,
	FSeinClusterFlowField& OutField) const
{
	OutField.Layer = Step.Layer;
	OutField.ClusterID = Step.ClusterID;
	OutField.GoalCellIndex = Step.GoalCellIndex;
	OutField.Directions.Reset();

	if (!Grid || !Grid->Layers.IsValidIndex(Step.Layer)) { return; }
	const FSeinNavigationLayer& L = Grid->Layers[Step.Layer];

	// Compute the tile bounds within the layer.
	const int32 TilesX = L.GetTileWidth();
	const int32 TileX = Step.ClusterID % TilesX;
	const int32 TileY = Step.ClusterID / TilesX;
	const int32 StartX = TileX * L.TileSize;
	const int32 StartY = TileY * L.TileSize;
	const int32 EndX = FMath::Min(StartX + L.TileSize, L.Width);
	const int32 EndY = FMath::Min(StartY + L.TileSize, L.Height);
	const int32 W = EndX - StartX;
	const int32 H = EndY - StartY;

	OutField.TileOriginXY = FIntPoint(StartX, StartY);
	OutField.TileExtent = FIntPoint(W, H);
	OutField.Directions.Init(FSeinClusterFlowField::DIR_UNREACHABLE, W * H);

	if (W <= 0 || H <= 0 || Step.GoalCellIndex == INDEX_NONE) { return; }

	// Translate goal cell to within-tile XY.
	const int32 GoalX = Step.GoalCellIndex % L.Width;
	const int32 GoalY = Step.GoalCellIndex / L.Width;
	const int32 LocalGoalX = GoalX - StartX;
	const int32 LocalGoalY = GoalY - StartY;
	if (LocalGoalX < 0 || LocalGoalX >= W || LocalGoalY < 0 || LocalGoalY >= H)
	{
		// Goal lies outside this cluster — shouldn't happen, but bail safely.
		return;
	}

	// Dijkstra from the goal outward (costs are symmetric: cell walkable = cost 1 + MovementCost).
	constexpr uint16 INF16 = 0xFFFF;
	TArray<uint16> Cost;
	Cost.Init(INF16, W * H);

	struct FOpen
	{
		int32 LocalIdx;
		uint16 Cost;
		bool operator<(const FOpen& Other) const { return Cost > Other.Cost; }
	};

	TArray<FOpen> Heap;
	Heap.Reserve(W * H / 4);
	Cost[LocalGoalY * W + LocalGoalX] = 0;
	Heap.HeapPush({ LocalGoalY * W + LocalGoalX, 0 }, TLess<FOpen>());

	static const int32 DX[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
	static const int32 DY[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

	while (Heap.Num() > 0)
	{
		FOpen Cur;
		Heap.HeapPop(Cur, TLess<FOpen>(), EAllowShrinking::No);
		if (Cur.Cost > Cost[Cur.LocalIdx]) { continue; }

		const int32 LX = Cur.LocalIdx % W;
		const int32 LY = Cur.LocalIdx / W;

		for (int32 d = 0; d < 8; ++d)
		{
			const int32 NLX = LX + DX[d];
			const int32 NLY = LY + DY[d];
			if (NLX < 0 || NLX >= W || NLY < 0 || NLY >= H) { continue; }

			const int32 GlobalX = StartX + NLX;
			const int32 GlobalY = StartY + NLY;
			const int32 GlobalIdx = GlobalY * L.Width + GlobalX;
			if (!L.IsCellWalkable(GlobalIdx)) { continue; }

			const uint16 StepCost = static_cast<uint16>(FMath::Max<uint8>(1, L.GetCellMovementCost(GlobalIdx)));
			const int32 NIdx = NLY * W + NLX;
			const uint16 New = Cur.Cost + StepCost;
			if (New < Cost[NIdx])
			{
				Cost[NIdx] = New;
				Heap.HeapPush({ NIdx, New }, TLess<FOpen>());
			}
		}
	}

	// Assign direction: for each cell, pick the 8-neighbor with the lowest cost
	// (steepest descent toward the goal). The goal cell itself gets direction 0 (arrived).
	for (int32 LY = 0; LY < H; ++LY)
	{
		for (int32 LX = 0; LX < W; ++LX)
		{
			const int32 LIdx = LY * W + LX;
			if (Cost[LIdx] == INF16) { continue; }
			if (LX == LocalGoalX && LY == LocalGoalY)
			{
				OutField.Directions[LIdx] = 0;
				continue;
			}

			int32 BestDir = FSeinClusterFlowField::DIR_UNREACHABLE;
			uint16 BestCost = Cost[LIdx];
			for (int32 d = 0; d < 8; ++d)
			{
				const int32 NLX = LX + DX[d];
				const int32 NLY = LY + DY[d];
				if (NLX < 0 || NLX >= W || NLY < 0 || NLY >= H) { continue; }
				const int32 NIdx = NLY * W + NLX;
				if (Cost[NIdx] < BestCost)
				{
					BestCost = Cost[NIdx];
					BestDir = d;
				}
			}
			OutField.Directions[LIdx] = static_cast<uint8>(BestDir);
		}
	}
}
