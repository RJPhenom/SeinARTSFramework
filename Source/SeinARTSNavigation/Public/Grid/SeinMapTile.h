/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMapTile.h
 * @brief   Tile partitioning summary — DESIGN.md §13 "Tile partitioning —
 *          shared spatial infrastructure". One tile summarizes a TileSize×TileSize
 *          block of cells on a single layer; used as broadphase by vision, spatial
 *          queries, pathfinding (HPA* cluster), and terrain-tag aggregate queries.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinMapTile.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinMapTile
{
	GENERATED_BODY()

	/** Entities currently occupying any cell inside this tile (for range-query broadphase). */
	UPROPERTY()
	TArray<FSeinEntityHandle> OccupyingEntities;

	/** Union of per-cell tags inside this tile (dirty-cached; rebuilt on cell-tag mutation). */
	UPROPERTY()
	FGameplayTagContainer AggregateTags;

	/** Packed bits indicating which VisionGroups have any emission source touching this tile.
	 *  Used by the vision system (§12) to skip dormant tiles during stamp ticks. */
	UPROPERTY()
	uint32 VisionGroupActiveMask = 0;

	/** HPA* cluster ID assigned at bake. 0 = ungrouped / Session 3.2 hasn't run yet. */
	UPROPERTY()
	int32 ClusterID = 0;

	/** Set when the tile's AggregateTags cache needs refresh after a cell-tag change. */
	UPROPERTY()
	uint8 bAggregateDirty : 1;

	/** Reserved — future use (e.g., per-tile clearance bounds cache). */
	UPROPERTY()
	uint8 bReserved : 1;

	FSeinMapTile()
		: VisionGroupActiveMask(0)
		, ClusterID(0)
		, bAggregateDirty(0)
		, bReserved(0)
	{}
};
