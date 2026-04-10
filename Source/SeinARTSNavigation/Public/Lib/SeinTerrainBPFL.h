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
#include "SeinTerrainBPFL.generated.h"

class USeinNavigationGrid;

UCLASS(meta = (DisplayName = "SeinARTS Terrain Library"))
class SEINARTSNAVIGATION_API USeinTerrainBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Get the terrain type tag at a world location */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein Get Terrain Type At"))
	static FGameplayTag SeinGetTerrainTypeAt(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Get the movement cost at a world location (0 = blocked, 1-255 = walkable) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein Get Movement Cost At"))
	static uint8 SeinGetMovementCostAt(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Check whether a world location is walkable */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein Is Location Walkable"))
	static bool SeinIsLocationWalkable(USeinNavigationGrid* Grid, FFixedVector Location);

	/** Find the nearest walkable location to a given point */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein Get Nearest Walkable Location"))
	static FFixedVector SeinGetNearestWalkableLocation(USeinNavigationGrid* Grid, FFixedVector Location, FFixedPoint SearchRadius);

	/** Convert a world location to grid coordinates */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein World To Grid"))
	static FIntPoint SeinWorldToGrid(USeinNavigationGrid* Grid, FFixedVector WorldLocation);

	/** Convert grid coordinates to a world-space center position */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Terrain", meta = (DisplayName = "Sein Grid To World"))
	static FFixedVector SeinGridToWorld(USeinNavigationGrid* Grid, FIntPoint GridPosition);
};
