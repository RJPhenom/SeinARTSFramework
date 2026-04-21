/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionBPFL.h
 * @brief   BP-facing vision queries (DESIGN.md §12).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinVisionBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Vision Library"))
class SEINARTSNAVIGATION_API USeinVisionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** True if the world-space location is currently visible in `Player`'s VisionGroup. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vision",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Location Visible"))
	static bool SeinIsLocationVisible(const UObject* WorldContextObject, FSeinPlayerID Player, FFixedVector WorldLocation);

	/** True if the world-space location has been seen at any past vision tick. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vision",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Location Explored"))
	static bool SeinIsLocationExplored(const UObject* WorldContextObject, FSeinPlayerID Player, FFixedVector WorldLocation);

	/** True if the entity is currently visible to `Player`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vision",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Entity Visible"))
	static bool SeinIsEntityVisible(const UObject* WorldContextObject, FSeinPlayerID Player, FSeinEntityHandle Entity);

	/** Entities currently visible to `Player` (ordered by entity index). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vision",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Visible Entities"))
	static TArray<FSeinEntityHandle> SeinGetVisibleEntities(const UObject* WorldContextObject, FSeinPlayerID Player);

	/** Vision-tick counter (debug / profiling). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Vision",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Vision Tick Count"))
	static int32 SeinGetVisionTickCount(const UObject* WorldContextObject);
};
