/**
 * ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
 * ██░▄▄▄░█░▄▄██▄██░▄▄▀█░▄▄▀██░▄▄▀█▄▄░▄▄██░▄▄▄░
 * ██▄▄▄▀▀█░▄▄██░▄█░██░█░▀▀░██░▀▀▄███░████▄▄▄▀▀
 * ██░▀▀▀░█▄▄▄█▄▄▄█▄██▄█░██░██░██░███░████░▀▀▀░
 * ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinNavigationBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for deterministic pathfinding and navigation queries.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinPathTypes.h"
#include "SeinNavigationBPFL.generated.h"

class USeinPathfinder;
class USeinNavigationGrid;

UCLASS(meta = (DisplayName = "SeinARTS Navigation Library"))
class SEINARTSNAVIGATION_API USeinNavigationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Pathfinding
	// ====================================================================================================

	/** Request a path from start to end. Returns a path result (check bIsValid). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation", meta = (DisplayName = "Request Path"))
	static FSeinPath SeinRequestPath(USeinPathfinder* Pathfinder, FFixedVector Start, FFixedVector End, FSeinEntityHandle Requester, FGameplayTagContainer BlockedTerrainTags);

	/** Check whether a path result is valid */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Is Path Valid"))
	static bool SeinIsPathValid(const FSeinPath& Path);

	/** Get the waypoints of a computed path */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Waypoints"))
	static TArray<FFixedVector> SeinGetPathWaypoints(const FSeinPath& Path);

	/** Get the total cost of a computed path */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Cost"))
	static FFixedPoint SeinGetPathCost(const FSeinPath& Path);

	/** Get the number of waypoints in a path */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Length"))
	static int32 SeinGetPathLength(const FSeinPath& Path);
};
