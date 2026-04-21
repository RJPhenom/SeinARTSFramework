/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainBPFL.cpp
 * @brief   Implementation of terrain query Blueprint nodes.
 */

#include "Lib/SeinTerrainBPFL.h"
#include "SeinNavigationGrid.h"
#include "SeinNavigationSubsystem.h"
#include "Components/SeinFootprintData.h"
#include "Events/SeinVisualEvent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Volumes/SeinTerrainVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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

namespace
{
	USeinNavigationGrid* ResolveGridFromContext(const UObject* WorldContextObject)
	{
		if (!WorldContextObject) { return nullptr; }
		const UWorld* World = WorldContextObject->GetWorld();
		if (!World) { return nullptr; }
		if (USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>())
		{
			return Nav->GetGrid();
		}
		return nullptr;
	}

	/** Apply a footprint rotation offset. */
	FIntPoint RotateOffset(const FIntPoint& Offset, uint8 Rotation)
	{
		switch (Rotation & 0x3)
		{
		default:
		case 0: return Offset;
		case 1: return FIntPoint(-Offset.Y,  Offset.X);   // 90° CCW
		case 2: return FIntPoint(-Offset.X, -Offset.Y);   // 180°
		case 3: return FIntPoint( Offset.Y, -Offset.X);   // 270° CCW
		}
	}
}

FSeinTerrainCoverQuery USeinTerrainBPFL::SeinQueryCoverAtLocation(const UObject* WorldContextObject, FFixedVector Location)
{
	FSeinTerrainCoverQuery Out;
	USeinNavigationGrid* Grid = ResolveGridFromContext(WorldContextObject);
	if (!Grid) { return Out; }

	const FIntPoint XY = Grid->WorldToGrid(Location);
	if (!Grid->IsValidCell(XY)) { return Out; }

	Out.Tags = Grid->GetCellTags(XY, /*Layer=*/0);

	// If any ASeinTerrainVolume covers this cell, adopt its CoverFacingDirection.
	if (const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr)
	{
		const FVector WorldLoc = Location.ToVector();
		for (TActorIterator<ASeinTerrainVolume> It(World); It; ++It)
		{
			const ASeinTerrainVolume* Vol = *It;
			if (!Vol) { continue; }
			const FBox Bounds = Vol->GetVolumeWorldBounds();
			if (Bounds.IsInsideOrOn(WorldLoc))
			{
				Out.CoverFacingDirection = Vol->CoverFacingDirection;
				break;
			}
		}
	}

	return Out;
}

void USeinTerrainBPFL::SeinRegisterFootprint(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector WorldLocation)
{
	USeinNavigationGrid* Grid = ResolveGridFromContext(WorldContextObject);
	if (!Grid) { return; }

	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	USeinWorldSubsystem* Sim = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!Sim) { return; }

	const FSeinFootprintData* Foot = Sim->GetComponent<FSeinFootprintData>(Entity);
	if (!Foot) { return; }

	const FIntPoint RootCell = Grid->WorldToGrid(WorldLocation);
	if (!Grid->IsValidCell(RootCell)) { return; }

	TArray<FIntPoint> ChangedCells;
	ChangedCells.Reserve(Foot->OccupiedCellOffsets.Num() + 1);

	// Stamp root + all offsets.
	TArray<FIntPoint> AllOffsets;
	AllOffsets.Reserve(Foot->OccupiedCellOffsets.Num() + 1);
	AllOffsets.Add(FIntPoint::ZeroValue);
	for (const FIntPoint& O : Foot->OccupiedCellOffsets)
	{
		AllOffsets.Add(RotateOffset(O, Foot->Rotation));
	}

	for (const FIntPoint& Off : AllOffsets)
	{
		const FIntPoint Target(RootCell.X + Off.X, RootCell.Y + Off.Y);
		if (!Grid->IsValidCell(Target)) { continue; }
		if (Foot->bBlocksPathing)
		{
			Grid->MarkCellBlocked(Target, 0);
		}
		ChangedCells.Add(Target);
	}

	// Fire TerrainMutated visual event (one per footprint op — consumers can bucket).
	if (ChangedCells.Num() > 0)
	{
		Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeTerrainMutatedEvent(WorldLocation, ChangedCells.Num()));
	}
}

void USeinTerrainBPFL::SeinUnregisterFootprint(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector WorldLocation)
{
	USeinNavigationGrid* Grid = ResolveGridFromContext(WorldContextObject);
	if (!Grid) { return; }

	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	USeinWorldSubsystem* Sim = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!Sim) { return; }

	const FSeinFootprintData* Foot = Sim->GetComponent<FSeinFootprintData>(Entity);
	if (!Foot) { return; }

	const FIntPoint RootCell = Grid->WorldToGrid(WorldLocation);
	if (!Grid->IsValidCell(RootCell)) { return; }

	TArray<FIntPoint> AllOffsets;
	AllOffsets.Reserve(Foot->OccupiedCellOffsets.Num() + 1);
	AllOffsets.Add(FIntPoint::ZeroValue);
	for (const FIntPoint& O : Foot->OccupiedCellOffsets)
	{
		AllOffsets.Add(RotateOffset(O, Foot->Rotation));
	}

	int32 UnblockedCount = 0;
	for (const FIntPoint& Off : AllOffsets)
	{
		const FIntPoint Target(RootCell.X + Off.X, RootCell.Y + Off.Y);
		if (!Grid->IsValidCell(Target)) { continue; }
		if (Foot->bBlocksPathing)
		{
			Grid->MarkCellUnblocked(Target, 0);
		}
		++UnblockedCount;
	}

	if (UnblockedCount > 0)
	{
		Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeTerrainMutatedEvent(WorldLocation, UnblockedCount));
	}
}
