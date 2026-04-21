#include "SeinNavigationGrid.h"

void USeinNavigationGrid::InitializeGrid(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin,
	ESeinElevationMode ElevationMode, int32 InTileSize)
{
	TArray<float> OneBaseline;
	OneBaseline.Add(Origin.Z.ToFloat());
	InitializeGridMultiLayer(Width, Height, InCellSize, Origin, ElevationMode, OneBaseline, InTileSize);
}

void USeinNavigationGrid::InitializeGridMultiLayer(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin,
	ESeinElevationMode ElevationMode, const TArray<float>& LayerBaselinesZ, int32 InTileSize)
{
	GridWidth = Width;
	GridHeight = Height;
	CellSize = InCellSize;
	GridOrigin = Origin;
	TileSize = InTileSize > 0 ? InTileSize : 32;

	// Palette slot 0 reserved for null.
	CellTagPalette.Reset();
	CellTagPalette.Add(FGameplayTag());

	Layers.Reset();
	Layers.SetNum(LayerBaselinesZ.Num());
	for (int32 i = 0; i < LayerBaselinesZ.Num(); ++i)
	{
		FSeinNavigationLayer& Layer = Layers[i];
		Layer.LayerBaselineZ = LayerBaselinesZ[i];
		Layer.Allocate(Width, Height, ElevationMode, TileSize);

		// Seed cells as walkable with cost 1 — bakes overwrite this, tests rely on it.
		const int32 Total = Width * Height;
		for (int32 c = 0; c < Total; ++c)
		{
			Layer.SetCellFlag(c, SeinCellFlag::Walkable, true);
			Layer.SetCellMovementCost(c, 1);
		}
	}
}

void USeinNavigationGrid::ClearLayers()
{
	Layers.Reset();
}

FIntPoint USeinNavigationGrid::WorldToGrid(const FFixedVector& WorldPos) const
{
	if (CellSize <= FFixedPoint::Zero)
	{
		return FIntPoint(INDEX_NONE, INDEX_NONE);
	}
	const FFixedPoint RelX = WorldPos.X - GridOrigin.X;
	const FFixedPoint RelY = WorldPos.Y - GridOrigin.Y;
	const int32 CellX = (RelX / CellSize).ToInt();
	const int32 CellY = (RelY / CellSize).ToInt();
	return FIntPoint(CellX, CellY);
}

FFixedVector USeinNavigationGrid::GridToWorld(FIntPoint GridPos) const
{
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

FSeinCellAddress USeinNavigationGrid::ResolveCellAddress(const FFixedVector& WorldPos) const
{
	const FIntPoint XY = WorldToGrid(WorldPos);
	if (!IsValidCell(XY) || Layers.Num() == 0)
	{
		return FSeinCellAddress::Invalid();
	}

	// Pick layer with highest baseline that is <= WorldPos.Z.
	const float Z = WorldPos.Z.ToFloat();
	int32 BestLayer = 0;
	float BestZ = -TNumericLimits<float>::Max();
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		const float BZ = Layers[i].LayerBaselineZ;
		if (BZ <= Z && BZ > BestZ)
		{
			BestZ = BZ;
			BestLayer = i;
		}
	}

	return FSeinCellAddress(static_cast<uint8>(BestLayer), CellIndex(XY));
}

bool USeinNavigationGrid::IsValidCell(FIntPoint GridPos) const
{
	return GridPos.X >= 0 && GridPos.X < GridWidth
		&& GridPos.Y >= 0 && GridPos.Y < GridHeight;
}

bool USeinNavigationGrid::IsWalkable(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos) || Layers.Num() == 0) { return false; }
	return Layers[0].IsCellWalkable(CellIndex(GridPos));
}

uint8 USeinNavigationGrid::GetMovementCost(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos) || Layers.Num() == 0) { return 0; }
	return Layers[0].GetCellMovementCost(CellIndex(GridPos));
}

FGameplayTag USeinNavigationGrid::GetTerrainType(FIntPoint GridPos) const
{
	if (!IsValidCell(GridPos) || Layers.Num() == 0) { return FGameplayTag(); }
	const int32 Idx = CellIndex(GridPos);
	const FSeinNavigationLayer& Layer = Layers[0];
	uint8 IdxA = 0;
	switch (Layer.ElevationMode)
	{
	case ESeinElevationMode::None:
		IdxA = Layer.CellsFlat.IsValidIndex(Idx) ? Layer.CellsFlat[Idx].TagIndexA : 0;
		break;
	case ESeinElevationMode::HeightOnly:
		IdxA = Layer.CellsHeight.IsValidIndex(Idx) ? Layer.CellsHeight[Idx].TagIndexA : 0;
		break;
	case ESeinElevationMode::HeightSlope:
		IdxA = Layer.CellsHeightSlope.IsValidIndex(Idx) ? Layer.CellsHeightSlope[Idx].TagIndexA : 0;
		break;
	}
	return CellTagPalette.IsValidIndex(IdxA) ? CellTagPalette[IdxA] : FGameplayTag();
}

FGameplayTagContainer USeinNavigationGrid::GetCellTags(FIntPoint GridPos, uint8 Layer) const
{
	FGameplayTagContainer Out;
	if (!IsValidCell(GridPos) || !Layers.IsValidIndex(Layer)) { return Out; }

	const FSeinNavigationLayer& L = Layers[Layer];
	const int32 Idx = CellIndex(GridPos);
	uint8 A = 0, B = 0;
	switch (L.ElevationMode)
	{
	case ESeinElevationMode::None:
		if (L.CellsFlat.IsValidIndex(Idx)) { A = L.CellsFlat[Idx].TagIndexA; B = L.CellsFlat[Idx].TagIndexB; }
		break;
	case ESeinElevationMode::HeightOnly:
		if (L.CellsHeight.IsValidIndex(Idx)) { A = L.CellsHeight[Idx].TagIndexA; B = L.CellsHeight[Idx].TagIndexB; }
		break;
	case ESeinElevationMode::HeightSlope:
		if (L.CellsHeightSlope.IsValidIndex(Idx)) { A = L.CellsHeightSlope[Idx].TagIndexA; B = L.CellsHeightSlope[Idx].TagIndexB; }
		break;
	}

	if (A != 0 && CellTagPalette.IsValidIndex(A)) { Out.AddTag(CellTagPalette[A]); }
	if (B != 0 && CellTagPalette.IsValidIndex(B)) { Out.AddTag(CellTagPalette[B]); }

	if (const FSeinCellTagOverflowEntry* Overflow = L.CellTagOverflow.Find(Idx))
	{
		for (uint8 PaletteIdx : Overflow->PaletteIndices)
		{
			if (PaletteIdx != 0 && CellTagPalette.IsValidIndex(PaletteIdx))
			{
				Out.AddTag(CellTagPalette[PaletteIdx]);
			}
		}
	}
	return Out;
}

uint8 USeinNavigationGrid::GetCellClearance(FIntPoint GridPos, uint8 Layer) const
{
	if (!IsValidCell(GridPos) || !Layers.IsValidIndex(Layer)) { return 0; }
	return Layers[Layer].GetCellClearance(CellIndex(GridPos));
}

bool USeinNavigationGrid::IsValidCellAddr(const FSeinCellAddress& Addr) const
{
	return Layers.IsValidIndex(Addr.Layer)
		&& Addr.CellIndex >= 0
		&& Addr.CellIndex < Layers[Addr.Layer].GetCellCount();
}

bool USeinNavigationGrid::IsWalkableAddr(const FSeinCellAddress& Addr) const
{
	return IsValidCellAddr(Addr) && Layers[Addr.Layer].IsCellWalkable(Addr.CellIndex);
}

FIntPoint USeinNavigationGrid::IndexToXY(int32 FlatIndex) const
{
	if (GridWidth <= 0) { return FIntPoint(INDEX_NONE, INDEX_NONE); }
	return FIntPoint(FlatIndex % GridWidth, FlatIndex / GridWidth);
}

void USeinNavigationGrid::MarkCellBlocked(FIntPoint GridPos, uint8 Layer)
{
	if (IsValidCell(GridPos) && Layers.IsValidIndex(Layer))
	{
		Layers[Layer].SetCellFlag(CellIndex(GridPos), SeinCellFlag::DynamicBlocked, true);
	}
}

void USeinNavigationGrid::MarkCellUnblocked(FIntPoint GridPos, uint8 Layer)
{
	if (IsValidCell(GridPos) && Layers.IsValidIndex(Layer))
	{
		Layers[Layer].SetCellFlag(CellIndex(GridPos), SeinCellFlag::DynamicBlocked, false);
	}
}

void USeinNavigationGrid::MarkAreaBlocked(FIntPoint Min, FIntPoint Max, uint8 Layer)
{
	if (!Layers.IsValidIndex(Layer)) { return; }
	const int32 MinX = FMath::Max(Min.X, 0);
	const int32 MinY = FMath::Max(Min.Y, 0);
	const int32 MaxX = FMath::Min(Max.X, GridWidth - 1);
	const int32 MaxY = FMath::Min(Max.Y, GridHeight - 1);
	FSeinNavigationLayer& L = Layers[Layer];
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			L.SetCellFlag(Y * GridWidth + X, SeinCellFlag::DynamicBlocked, true);
		}
	}
}

void USeinNavigationGrid::MarkAreaUnblocked(FIntPoint Min, FIntPoint Max, uint8 Layer)
{
	if (!Layers.IsValidIndex(Layer)) { return; }
	const int32 MinX = FMath::Max(Min.X, 0);
	const int32 MinY = FMath::Max(Min.Y, 0);
	const int32 MaxX = FMath::Min(Max.X, GridWidth - 1);
	const int32 MaxY = FMath::Min(Max.Y, GridHeight - 1);
	FSeinNavigationLayer& L = Layers[Layer];
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			L.SetCellFlag(Y * GridWidth + X, SeinCellFlag::DynamicBlocked, false);
		}
	}
}

int32 USeinNavigationGrid::CellIndex(FIntPoint GridPos) const
{
	return GridPos.Y * GridWidth + GridPos.X;
}

bool USeinNavigationGrid::IsValidIndex(int32 Index, uint8 Layer) const
{
	return Layers.IsValidIndex(Layer) && Index >= 0 && Index < Layers[Layer].GetCellCount();
}

TArray<FIntPoint> USeinNavigationGrid::GetNeighbors(FIntPoint GridPos, bool bIncludeDiagonals) const
{
	TArray<FIntPoint> Neighbors;
	Neighbors.Reserve(bIncludeDiagonals ? 8 : 4);

	static const FIntPoint CardinalOffsets[] = {
		FIntPoint( 0,  1), FIntPoint( 1,  0), FIntPoint( 0, -1), FIntPoint(-1,  0),
	};
	static const FIntPoint DiagonalOffsets[] = {
		FIntPoint( 1,  1), FIntPoint( 1, -1), FIntPoint(-1, -1), FIntPoint(-1,  1),
	};

	for (const FIntPoint& Offset : CardinalOffsets)
	{
		const FIntPoint Neighbor(GridPos.X + Offset.X, GridPos.Y + Offset.Y);
		if (IsValidCell(Neighbor)) { Neighbors.Add(Neighbor); }
	}
	if (bIncludeDiagonals)
	{
		for (const FIntPoint& Offset : DiagonalOffsets)
		{
			const FIntPoint Neighbor(GridPos.X + Offset.X, GridPos.Y + Offset.Y);
			if (IsValidCell(Neighbor)) { Neighbors.Add(Neighbor); }
		}
	}
	return Neighbors;
}

uint8 USeinNavigationGrid::ResolveOrAddPaletteIndex(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) { return 0; }
	const int32 Existing = CellTagPalette.IndexOfByKey(Tag);
	if (Existing != INDEX_NONE) { return static_cast<uint8>(Existing); }
	if (CellTagPalette.Num() >= 255)
	{
		return 0; // palette full — designer will need to switch to u16 variant (future).
	}
	const int32 NewIdx = CellTagPalette.Add(Tag);
	return static_cast<uint8>(NewIdx);
}

void USeinNavigationGrid::AddCellTag(FIntPoint GridPos, const FGameplayTag& Tag, uint8 Layer)
{
	if (!IsValidCell(GridPos) || !Layers.IsValidIndex(Layer) || !Tag.IsValid()) { return; }
	const uint8 PaletteIdx = ResolveOrAddPaletteIndex(Tag);
	if (PaletteIdx == 0) { return; }

	FSeinNavigationLayer& L = Layers[Layer];
	const int32 Idx = CellIndex(GridPos);

	auto TryAddInline = [PaletteIdx](uint8& A, uint8& B) -> bool
	{
		if (A == PaletteIdx || B == PaletteIdx) { return true; }
		if (A == 0) { A = PaletteIdx; return true; }
		if (B == 0) { B = PaletteIdx; return true; }
		return false;
	};

	bool bPlaced = false;
	switch (L.ElevationMode)
	{
	case ESeinElevationMode::None:
		if (L.CellsFlat.IsValidIndex(Idx)) { bPlaced = TryAddInline(L.CellsFlat[Idx].TagIndexA, L.CellsFlat[Idx].TagIndexB); }
		break;
	case ESeinElevationMode::HeightOnly:
		if (L.CellsHeight.IsValidIndex(Idx)) { bPlaced = TryAddInline(L.CellsHeight[Idx].TagIndexA, L.CellsHeight[Idx].TagIndexB); }
		break;
	case ESeinElevationMode::HeightSlope:
		if (L.CellsHeightSlope.IsValidIndex(Idx)) { bPlaced = TryAddInline(L.CellsHeightSlope[Idx].TagIndexA, L.CellsHeightSlope[Idx].TagIndexB); }
		break;
	}

	if (!bPlaced)
	{
		FSeinCellTagOverflowEntry& Overflow = L.CellTagOverflow.FindOrAdd(Idx);
		if (!Overflow.PaletteIndices.Contains(PaletteIdx)) { Overflow.PaletteIndices.Add(PaletteIdx); }
	}

	// Mark the tile aggregate cache dirty.
	const int32 TX = GridPos.X / L.TileSize;
	const int32 TY = GridPos.Y / L.TileSize;
	const int32 TileIdx = TY * L.GetTileWidth() + TX;
	if (L.Tiles.IsValidIndex(TileIdx))
	{
		L.Tiles[TileIdx].bAggregateDirty = 1;
	}
}

void USeinNavigationGrid::RemoveCellTag(FIntPoint GridPos, const FGameplayTag& Tag, uint8 Layer)
{
	if (!IsValidCell(GridPos) || !Layers.IsValidIndex(Layer) || !Tag.IsValid()) { return; }
	const int32 PaletteIdx = CellTagPalette.IndexOfByKey(Tag);
	if (PaletteIdx <= 0) { return; }
	const uint8 PIdx = static_cast<uint8>(PaletteIdx);

	FSeinNavigationLayer& L = Layers[Layer];
	const int32 Idx = CellIndex(GridPos);

	auto StripInline = [PIdx](uint8& A, uint8& B)
	{
		if (A == PIdx) { A = 0; }
		if (B == PIdx) { B = 0; }
	};

	switch (L.ElevationMode)
	{
	case ESeinElevationMode::None:
		if (L.CellsFlat.IsValidIndex(Idx)) { StripInline(L.CellsFlat[Idx].TagIndexA, L.CellsFlat[Idx].TagIndexB); }
		break;
	case ESeinElevationMode::HeightOnly:
		if (L.CellsHeight.IsValidIndex(Idx)) { StripInline(L.CellsHeight[Idx].TagIndexA, L.CellsHeight[Idx].TagIndexB); }
		break;
	case ESeinElevationMode::HeightSlope:
		if (L.CellsHeightSlope.IsValidIndex(Idx)) { StripInline(L.CellsHeightSlope[Idx].TagIndexA, L.CellsHeightSlope[Idx].TagIndexB); }
		break;
	}

	if (FSeinCellTagOverflowEntry* Overflow = L.CellTagOverflow.Find(Idx))
	{
		Overflow->PaletteIndices.Remove(PIdx);
		if (Overflow->PaletteIndices.Num() == 0) { L.CellTagOverflow.Remove(Idx); }
	}

	const int32 TX = GridPos.X / L.TileSize;
	const int32 TY = GridPos.Y / L.TileSize;
	const int32 TileIdx = TY * L.GetTileWidth() + TX;
	if (L.Tiles.IsValidIndex(TileIdx))
	{
		L.Tiles[TileIdx].bAggregateDirty = 1;
	}
}

const FSeinNavigationLayer* USeinNavigationGrid::GetLayer(uint8 Layer) const
{
	return Layers.IsValidIndex(Layer) ? &Layers[Layer] : nullptr;
}

FSeinNavigationLayer* USeinNavigationGrid::GetLayerMutable(uint8 Layer)
{
	return Layers.IsValidIndex(Layer) ? &Layers[Layer] : nullptr;
}

int32 USeinNavigationGrid::ClusterIDForCell(const FSeinCellAddress& Addr) const
{
	if (!IsValidCellAddr(Addr)) { return INDEX_NONE; }
	const FSeinNavigationLayer& L = Layers[Addr.Layer];
	const int32 CellX = Addr.CellIndex % L.Width;
	const int32 CellY = Addr.CellIndex / L.Width;
	const int32 TileX = CellX / L.TileSize;
	const int32 TileY = CellY / L.TileSize;
	return TileY * L.GetTileWidth() + TileX;
}

bool USeinNavigationGrid::IsReachable(
	const FSeinCellAddress& From,
	const FSeinCellAddress& To,
	const FGameplayTagContainer& AgentTags) const
{
	if (!IsValidCellAddr(From) || !IsValidCellAddr(To))
	{
		return false;
	}
	if (!IsWalkableAddr(From) || !IsWalkableAddr(To))
	{
		return false;
	}

	const int32 FromCluster = ClusterIDForCell(From);
	const int32 ToCluster = ClusterIDForCell(To);

	if (FromCluster == INDEX_NONE || ToCluster == INDEX_NONE)
	{
		return false;
	}

	// Same cluster — assume reachable intra-cluster for MVP (refined in 3.3 with flow fields).
	if (From.Layer == To.Layer && FromCluster == ToCluster)
	{
		return true;
	}

	const int32 FromNodeIdx = AbstractGraph.FindNode(From.Layer, FromCluster);
	const int32 ToNodeIdx = AbstractGraph.FindNode(To.Layer, ToCluster);
	if (FromNodeIdx == INDEX_NONE || ToNodeIdx == INDEX_NONE)
	{
		// No graph built (pre-bake) or one endpoint has no walkable cells.
		return false;
	}

	// BFS.
	TSet<int32> Visited;
	TArray<int32> Frontier;
	Visited.Reserve(AbstractGraph.Nodes.Num());
	Frontier.Reserve(32);

	Visited.Add(FromNodeIdx);
	Frontier.Add(FromNodeIdx);

	while (Frontier.Num() > 0)
	{
		const int32 CurIdx = Frontier.Pop(EAllowShrinking::No);
		if (CurIdx == ToNodeIdx) { return true; }

		const FSeinAbstractNode& CurNode = AbstractGraph.Nodes[CurIdx];
		for (int32 EdgeIdx : CurNode.OutgoingEdgeIndices)
		{
			if (!AbstractGraph.Edges.IsValidIndex(EdgeIdx)) { continue; }
			const FSeinAbstractEdge& E = AbstractGraph.Edges[EdgeIdx];
			if (!E.bEnabled) { continue; }

			// Navlink eligibility filter.
			if (E.EdgeType == ESeinAbstractEdgeType::NavLink && !E.EligibilityQuery.IsEmpty())
			{
				if (!E.EligibilityQuery.Matches(AgentTags))
				{
					continue;
				}
			}

			if (!Visited.Contains(E.ToNode))
			{
				if (E.ToNode == ToNodeIdx) { return true; }
				Visited.Add(E.ToNode);
				Frontier.Add(E.ToNode);
			}
		}
	}

	return false;
}

bool USeinNavigationGrid::IsLocationReachable(
	const FFixedVector& From,
	const FFixedVector& To,
	const FGameplayTagContainer& AgentTags) const
{
	const FSeinCellAddress FromAddr = ResolveCellAddress(From);
	const FSeinCellAddress ToAddr = ResolveCellAddress(To);
	return IsReachable(FromAddr, ToAddr, AgentTags);
}

bool USeinNavigationGrid::SetNavLinkEnabled(int32 NavLinkID, bool bEnabled)
{
	if (!NavLinks.IsValidIndex(NavLinkID)) { return false; }
	FSeinNavLinkRecord& Rec = NavLinks[NavLinkID];
	if (Rec.bEnabled == bEnabled) { return true; }
	Rec.bEnabled = bEnabled;
	// Propagate to abstract-graph edges.
	for (int32 EdgeIdx : Rec.EdgeIndices)
	{
		if (AbstractGraph.Edges.IsValidIndex(EdgeIdx))
		{
			AbstractGraph.Edges[EdgeIdx].bEnabled = bEnabled;
		}
	}
	return true;
}
