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
class USeinNavigationGridAsset;
class USeinFlowFieldPlanner;

UCLASS()
class SEINARTSNAVIGATION_API USeinNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

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

	/** Apply a baked grid asset to the runtime grid (used by level-load path). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation")
	void ApplyGridAsset(USeinNavigationGridAsset* Asset);

	/** Find the first baked grid asset referenced by any ASeinNavVolume in the world. */
	USeinNavigationGridAsset* FindBakedGridAssetInWorld() const;

	/** Flow-field planner (HPA* + Dijkstra + per-broker plan cache). */
	USeinFlowFieldPlanner* GetFlowFieldPlanner() const { return FlowPlanner; }

private:
	UPROPERTY()
	TObjectPtr<USeinNavigationGrid> Grid;

	UPROPERTY()
	TObjectPtr<USeinPathfinder> Pathfinder;

	UPROPERTY()
	TObjectPtr<USeinFlowFieldPlanner> FlowPlanner;
};
