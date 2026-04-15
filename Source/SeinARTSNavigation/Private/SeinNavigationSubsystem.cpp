#include "SeinNavigationSubsystem.h"
#include "SeinPathfinder.h"
#include "SeinNavigationGrid.h"

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
}

void USeinNavigationSubsystem::Deinitialize()
{
	Pathfinder = nullptr;
	Grid = nullptr;
	Super::Deinitialize();
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
