#include "Lib/SeinSpatialBPFL.h"

#include "SeinNavigationSubsystem.h"
#include "SeinNavigationGrid.h"
#include "Grid/SeinNavigationLayer.h"
#include "Grid/SeinMapTile.h"
#include "Bake/SeinNavBaker.h"
#include "Engine/World.h"
#include "Algo/Sort.h"

USeinNavigationGrid* USeinSpatialBPFL::GetGrid(const UObject* WorldContextObject)
{
	if (!WorldContextObject) { return nullptr; }
	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return nullptr; }
	const USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>();
	return Nav ? Nav->GetGrid() : nullptr;
}

TArray<FSeinEntityHandle> USeinSpatialBPFL::SeinQueryEntitiesInRadius(
	const UObject* WorldContextObject,
	FFixedVector Center,
	FFixedPoint Radius)
{
	TArray<FSeinEntityHandle> Out;
	USeinNavigationGrid* Grid = GetGrid(WorldContextObject);
	if (!Grid || Grid->Layers.Num() == 0) { return Out; }

	const FSeinNavigationLayer& L = Grid->Layers[0];
	if (L.Width == 0 || L.Height == 0) { return Out; }

	// Bbox of the query circle in cell space.
	const FFixedPoint CellSize = Grid->CellSize;
	if (CellSize <= FFixedPoint::Zero) { return Out; }

	const FFixedPoint RelMinX = (Center.X - Radius) - Grid->GridOrigin.X;
	const FFixedPoint RelMinY = (Center.Y - Radius) - Grid->GridOrigin.Y;
	const FFixedPoint RelMaxX = (Center.X + Radius) - Grid->GridOrigin.X;
	const FFixedPoint RelMaxY = (Center.Y + Radius) - Grid->GridOrigin.Y;

	const int32 CellMinX = FMath::Clamp((RelMinX / CellSize).ToInt(), 0, L.Width - 1);
	const int32 CellMinY = FMath::Clamp((RelMinY / CellSize).ToInt(), 0, L.Height - 1);
	const int32 CellMaxX = FMath::Clamp((RelMaxX / CellSize).ToInt(), 0, L.Width - 1);
	const int32 CellMaxY = FMath::Clamp((RelMaxY / CellSize).ToInt(), 0, L.Height - 1);

	// Expand to tile bbox.
	const int32 TileMinX = CellMinX / L.TileSize;
	const int32 TileMinY = CellMinY / L.TileSize;
	const int32 TileMaxX = CellMaxX / L.TileSize;
	const int32 TileMaxY = CellMaxY / L.TileSize;
	const int32 TilesX = L.GetTileWidth();

	const FFixedPoint RadiusSq = Radius * Radius;
	TSet<FSeinEntityHandle> Seen;
	Seen.Reserve(32);

	for (int32 TY = TileMinY; TY <= TileMaxY; ++TY)
	{
		for (int32 TX = TileMinX; TX <= TileMaxX; ++TX)
		{
			const int32 TileIdx = TY * TilesX + TX;
			if (!L.Tiles.IsValidIndex(TileIdx)) { continue; }
			const FSeinMapTile& Tile = L.Tiles[TileIdx];
			for (const FSeinEntityHandle& H : Tile.OccupyingEntities)
			{
				if (Seen.Contains(H)) { continue; }
				Seen.Add(H);
				// Precise per-entity position check is deferred to the caller — the sim
				// owns entity positions. Broadphase returns tile-level superset; caller
				// filters via its own FFixedTransform reads. For MVP, return every
				// entity whose tile overlaps the bbox.
				Out.Add(H);
			}
		}
	}

	// Deterministic order.
	Out.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B) { return A.Index < B.Index; });
	return Out;
}

void USeinSpatialBPFL::SeinRegisterEntityInGrid(
	const UObject* WorldContextObject,
	FSeinEntityHandle Handle,
	FFixedVector WorldLocation)
{
	USeinNavigationGrid* Grid = GetGrid(WorldContextObject);
	if (!Grid || Grid->Layers.Num() == 0) { return; }

	const FIntPoint Cell = Grid->WorldToGrid(WorldLocation);
	if (!Grid->IsValidCell(Cell)) { return; }

	FSeinNavigationLayer& L = Grid->Layers[0];
	const int32 TX = Cell.X / L.TileSize;
	const int32 TY = Cell.Y / L.TileSize;
	const int32 TileIdx = TY * L.GetTileWidth() + TX;
	if (L.Tiles.IsValidIndex(TileIdx))
	{
		FSeinMapTile& Tile = L.Tiles[TileIdx];
		Tile.OccupyingEntities.AddUnique(Handle);
	}
}

void USeinSpatialBPFL::SeinUnregisterEntityFromGrid(
	const UObject* WorldContextObject,
	FSeinEntityHandle Handle,
	FFixedVector WorldLocation)
{
	USeinNavigationGrid* Grid = GetGrid(WorldContextObject);
	if (!Grid || Grid->Layers.Num() == 0) { return; }

	const FIntPoint Cell = Grid->WorldToGrid(WorldLocation);
	if (!Grid->IsValidCell(Cell)) { return; }

	FSeinNavigationLayer& L = Grid->Layers[0];
	const int32 TX = Cell.X / L.TileSize;
	const int32 TY = Cell.Y / L.TileSize;
	const int32 TileIdx = TY * L.GetTileWidth() + TX;
	if (L.Tiles.IsValidIndex(TileIdx))
	{
		L.Tiles[TileIdx].OccupyingEntities.RemoveSingleSwap(Handle);
	}
}

void USeinSpatialBPFL::SeinRebuildNav(const UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return; }
	USeinNavBaker::BeginBake(World);
}

bool USeinSpatialBPFL::SeinIsBakingNav()
{
	return USeinNavBaker::IsBaking();
}
