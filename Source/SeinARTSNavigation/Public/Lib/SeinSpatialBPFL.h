/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSpatialBPFL.h
 * @brief   Tile-broadphase spatial queries. Per DESIGN.md §13 "Tile partitioning"
 *          — iterate tiles in the query's bbox, drill into cells, filter entities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinSpatialBPFL.generated.h"

class USeinNavigationGrid;

UCLASS(meta = (DisplayName = "SeinARTS Spatial Library"))
class SEINARTSNAVIGATION_API USeinSpatialBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Entities whose tile-summary footprint overlaps a circle at Center with Radius.
	 * Uses the layer-0 tile grid as broadphase, then tests per-entity position.
	 * Returns handles in deterministic order (sorted by entity index).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Spatial",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Entities In Radius"))
	static TArray<FSeinEntityHandle> SeinQueryEntitiesInRadius(
		const UObject* WorldContextObject,
		FFixedVector Center,
		FFixedPoint Radius);

	/** Register an entity with the grid's tile summaries at its current sim position. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Spatial",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Register Entity In Spatial Grid"))
	static void SeinRegisterEntityInGrid(
		const UObject* WorldContextObject,
		FSeinEntityHandle Handle,
		FFixedVector WorldLocation);

	/** Remove an entity from the grid's tile summaries. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Spatial",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Unregister Entity From Spatial Grid"))
	static void SeinUnregisterEntityFromGrid(
		const UObject* WorldContextObject,
		FSeinEntityHandle Handle,
		FFixedVector WorldLocation);

	// --- Bake control (BP-callable; primarily used from editor, but exposed for
	//     user-generated-map scenarios in shipped games too.) ---

	/** Kick off a nav bake over every ASeinNavVolume in the context world. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Spatial|Bake",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Rebuild Sein Nav"))
	static void SeinRebuildNav(const UObject* WorldContextObject);

	/** True while a bake is in progress. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Spatial|Bake",
		meta = (DisplayName = "Is Baking Nav"))
	static bool SeinIsBakingNav();

private:
	static USeinNavigationGrid* GetGrid(const UObject* WorldContextObject);
};
