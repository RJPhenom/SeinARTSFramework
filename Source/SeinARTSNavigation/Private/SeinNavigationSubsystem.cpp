#include "SeinNavigationSubsystem.h"
#include "SeinPathfinder.h"
#include "SeinNavigationGrid.h"
#include "SeinFlowFieldPlanner.h"
#include "Grid/SeinNavigationGridAsset.h"
#include "Grid/SeinMapTile.h"
#include "Grid/SeinNavigationLayer.h"
#include "Volumes/SeinNavVolume.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "EngineUtils.h"

void USeinNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Grid = NewObject<USeinNavigationGrid>(this, TEXT("SeinDefaultNavGrid"));
	Grid->InitializeGrid(
		/*Width=*/256,
		/*Height=*/256,
		/*CellSize=*/FFixedPoint::One,
		/*Origin=*/FFixedVector(FFixedPoint::FromInt(-128), FFixedPoint::FromInt(-128), FFixedPoint::Zero)
	);

	Pathfinder = NewObject<USeinPathfinder>(this, TEXT("SeinDefaultPathfinder"));
	Pathfinder->Initialize(Grid);

	FlowPlanner = NewObject<USeinFlowFieldPlanner>(this, TEXT("SeinDefaultFlowPlanner"));
	FlowPlanner->Initialize(Grid);
}

void USeinNavigationSubsystem::Deinitialize()
{
	FlowPlanner = nullptr;
	Pathfinder = nullptr;
	Grid = nullptr;
	Super::Deinitialize();
}

void USeinNavigationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Auto-apply any baked grid asset referenced by NavVolumes in this world.
	if (USeinNavigationGridAsset* BakedAsset = FindBakedGridAssetInWorld())
	{
		ApplyGridAsset(BakedAsset);
	}

	// Register the pathable-target resolver with the sim so
	// USeinAbility::bRequiresPathableTarget can consult nav reachability at
	// command-dispatch time without a cross-module dependency.
	if (USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>())
	{
		Sim->PathableTargetResolver.BindWeakLambda(this,
			[this](const FVector& FromWorld, const FVector& ToWorld, const FGameplayTagContainer& AgentTags) -> bool
			{
				if (!Grid)
				{
					// No grid loaded yet — permit the command (tests, nav-less games).
					return true;
				}
				const FFixedVector From(
					FFixedPoint::FromFloat(FromWorld.X),
					FFixedPoint::FromFloat(FromWorld.Y),
					FFixedPoint::FromFloat(FromWorld.Z));
				const FFixedVector To(
					FFixedPoint::FromFloat(ToWorld.X),
					FFixedPoint::FromFloat(ToWorld.Y),
					FFixedPoint::FromFloat(ToWorld.Z));
				return Grid->IsLocationReachable(From, To, AgentTags);
			});

		// Spatial-grid (un)registration callbacks (§14 containment visibility
		// transitions). Looks up the entity's current location on the fly so
		// Hidden-mode occupants exit + re-enter the tile grid at the right cell.
		TWeakObjectPtr<USeinWorldSubsystem> SimWeak = Sim;
		Sim->SpatialGridRegisterCallback.BindWeakLambda(this,
			[this, SimWeak](FSeinEntityHandle Handle)
			{
				if (!Grid || Grid->Layers.Num() == 0) return;
				USeinWorldSubsystem* S = SimWeak.Get();
				if (!S) return;
				const FSeinEntity* E = S->GetEntity(Handle);
				if (!E) return;
				const FIntPoint Cell = Grid->WorldToGrid(E->Transform.GetLocation());
				if (!Grid->IsValidCell(Cell)) return;
				FSeinNavigationLayer& L = Grid->Layers[0];
				const int32 TileIdx = (Cell.Y / L.TileSize) * L.GetTileWidth() + (Cell.X / L.TileSize);
				if (L.Tiles.IsValidIndex(TileIdx))
				{
					L.Tiles[TileIdx].OccupyingEntities.AddUnique(Handle);
				}
			});
		Sim->SpatialGridUnregisterCallback.BindWeakLambda(this,
			[this, SimWeak](FSeinEntityHandle Handle)
			{
				if (!Grid || Grid->Layers.Num() == 0) return;
				USeinWorldSubsystem* S = SimWeak.Get();
				if (!S) return;
				const FSeinEntity* E = S->GetEntity(Handle);
				if (!E) return;
				const FIntPoint Cell = Grid->WorldToGrid(E->Transform.GetLocation());
				if (!Grid->IsValidCell(Cell)) return;
				FSeinNavigationLayer& L = Grid->Layers[0];
				const int32 TileIdx = (Cell.Y / L.TileSize) * L.GetTileWidth() + (Cell.X / L.TileSize);
				if (L.Tiles.IsValidIndex(TileIdx))
				{
					L.Tiles[TileIdx].OccupyingEntities.RemoveSingleSwap(Handle);
				}
			});
	}
}

void USeinNavigationSubsystem::RebuildGrid(int32 Width, int32 Height, float CellSize, FVector Origin)
{
	if (!Grid)
	{
		Grid = NewObject<USeinNavigationGrid>(this, TEXT("SeinDefaultNavGrid"));
	}
	Grid->InitializeGrid(
		Width,
		Height,
		FFixedPoint::FromFloat(CellSize),
		FFixedVector(FFixedPoint::FromFloat(Origin.X), FFixedPoint::FromFloat(Origin.Y), FFixedPoint::FromFloat(Origin.Z))
	);
	if (!Pathfinder)
	{
		Pathfinder = NewObject<USeinPathfinder>(this, TEXT("SeinDefaultPathfinder"));
	}
	Pathfinder->Initialize(Grid);
}

void USeinNavigationSubsystem::ApplyGridAsset(USeinNavigationGridAsset* Asset)
{
	if (!Asset)
	{
		return;
	}
	if (!Grid)
	{
		Grid = NewObject<USeinNavigationGrid>(this, TEXT("SeinDefaultNavGrid"));
	}

	Grid->GridWidth = Asset->GridWidth;
	Grid->GridHeight = Asset->GridHeight;
	Grid->CellSize = FFixedPoint::FromFloat(Asset->CellSize);
	Grid->GridOrigin = FFixedVector(
		FFixedPoint::FromFloat(Asset->GridOrigin.X),
		FFixedPoint::FromFloat(Asset->GridOrigin.Y),
		FFixedPoint::FromFloat(Asset->GridOrigin.Z));
	Grid->TileSize = Asset->TileSize;
	Grid->CellTagPalette = Asset->CellTagPalette;
	Grid->Layers = Asset->Layers;
	Grid->AbstractGraph = Asset->AbstractGraph;
	Grid->NavLinks = Asset->NavLinks;

	if (!Pathfinder)
	{
		Pathfinder = NewObject<USeinPathfinder>(this, TEXT("SeinDefaultPathfinder"));
	}
	Pathfinder->Initialize(Grid);

	if (FlowPlanner)
	{
		FlowPlanner->Initialize(Grid);
	}
}

USeinNavigationGridAsset* USeinNavigationSubsystem::FindBakedGridAssetInWorld() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASeinNavVolume> It(World); It; ++It)
		{
			if (USeinNavigationGridAsset* Asset = It->BakedGridAsset)
			{
				return Asset;
			}
		}
	}
	return nullptr;
}
