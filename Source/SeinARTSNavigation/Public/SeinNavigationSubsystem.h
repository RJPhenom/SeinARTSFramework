/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.h
 * @brief   World subsystem owning the runtime pathfinder + navigation grid.
 *
 * SeinARTSNavigation is a sim-parallel module that cannot be referenced from
 * SeinARTSCoreEntity (which owns USeinWorldSubsystem). We therefore bootstrap
 * the pathfinder from a separate UWorldSubsystem that lives in this module.
 *
 * Usage:
 *   USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>();
 *   USeinPathfinder* PF = Nav->GetPathfinder();
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationSubsystem.generated.h"

class USeinPathfinder;
class USeinNavigationGrid;

UCLASS()
class SEINARTSNAVIGATION_API USeinNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Get the runtime pathfinder. Never null after Initialize(). */
	USeinPathfinder* GetPathfinder() const { return Pathfinder; }

	/** Get the active navigation grid. Never null after Initialize(). */
	USeinNavigationGrid* GetGrid() const { return Grid; }

	/**
	 * Re-initialize the grid at a different size/origin. Called by level scripts or
	 * editor tools to match a specific map. Default grid is a 256x256 empty field
	 * centered on the origin with CellSize 1 — suitable for basic test maps.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation")
	void RebuildGrid(int32 Width, int32 Height, float CellSize, FVector Origin);

private:
	UPROPERTY()
	TObjectPtr<USeinNavigationGrid> Grid;

	UPROPERTY()
	TObjectPtr<USeinPathfinder> Pathfinder;
};
