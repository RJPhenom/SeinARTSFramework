/**
 * ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
 * ██░▄▄▄░█░▄▄██▄██░▄▄▀█░▄▄▀██░▄▄▀█▄▄░▄▄██░▄▄▄░
 * ██▄▄▄▀▀█░▄▄██░▄█░██░█░▀▀░██░▀▀▄███░████▄▄▄▀▀
 * ██░▀▀▀░█▄▄▄█▄▄▄█▄██▄█░██░██░██░███░████░▀▀▀░
 * ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinTerrainBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for terrain queries on the navigation grid.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinTerrainBPFL.generated.h"

class USeinNavigationGrid;

/** Cover query result — tags at a cell + optional cover-facing direction. DESIGN §13. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinTerrainCoverQuery
{
	GENERATED_BODY()

	/** Cover + biome tags at the queried location. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FGameplayTagContainer Tags;

	/** Unit vector along which cover is oriented (non-directional = zero). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Terrain")
	FFixedVector CoverFacingDirection;
};

UCLASS(meta = (DisplayName = "SeinARTS Terrain Library"))
class SEINARTSNAVIGATION_API USeinTerrainBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Get the terrain type tag at a world location */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Get Terrain Type At"))
	static FGameplayTag SeinGetTerrainTypeAt(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Get the movement cost at a world location (0 = blocked, 1-255 = walkable) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Get Movement Cost At"))
	static uint8 SeinGetMovementCostAt(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Check whether a world location is walkable */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Is Location Walkable"))
	static bool SeinIsLocationWalkable(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Find the nearest walkable location to a given point */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Terrain", meta = (DisplayName = "Get Nearest Walkable Location"))
	static FFixedVector SeinGetNearestWalkableLocation(USeinNavigationGrid* Grid, FFixedVector Location, FFixedPoint SearchRadius);

	/** Convert a world location to grid coordinates */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "World To Grid"))
	static FIntPoint SeinWorldToGrid(USeinNavigationGrid* Grid, FFixedVector WorldLocation);

	/** Convert grid coordinates to a world-space center position */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Grid To World"))
	static FFixedVector SeinGridToWorld(USeinNavigationGrid* Grid, FIntPoint GridPosition);

	/** Query cover tags + facing direction at a world location (DESIGN §13).
	 *  CoverFacingDirection is zero for non-directional cover. Designer computes
	 *  the dot-product against their attack vector for directional-cover math. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Terrain",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Cover At Location"))
	static FSeinTerrainCoverQuery SeinQueryCoverAtLocation(const UObject* WorldContextObject, FFixedVector Location);

	/** Register a multi-cell footprint — marks cells DynamicBlocked for pathing. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Terrain|Footprint",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Register Footprint"))
	static void SeinRegisterFootprint(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector WorldLocation);

	/** Unregister a previously-registered footprint. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Terrain|Footprint",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Unregister Footprint"))
	static void SeinUnregisterFootprint(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector WorldLocation);
};
