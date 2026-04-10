/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinCombatBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for combat operations and spatial queries.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinCombatBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Combat Library"))
class SEINARTSCOREENTITY_API USeinCombatBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Damage
	// ====================================================================================================

	/** Apply damage to an entity. Fires a DamageTaken visual event. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Apply Damage"))
	static void SeinApplyDamage(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Damage, FSeinEntityHandle SourceHandle, FGameplayTag DamageTag);

	/** Apply healing to an entity. Fires a Healed visual event. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Apply Healing"))
	static void SeinApplyHealing(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FSeinEntityHandle SourceHandle);

	// Spatial Queries
	// ====================================================================================================

	/** Get all entities within a radius of an origin point, optionally filtered by tags */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Entities In Range"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInRange(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	/** Get the nearest entity to an origin point within a radius, optionally filtered by tags */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Nearest Entity"))
	static FSeinEntityHandle SeinGetNearestEntity(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	/** Get all entities within an axis-aligned box, optionally filtered by tags */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Entities In Box"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInBox(const UObject* WorldContextObject, FFixedVector Min, FFixedVector Max, FGameplayTagContainer FilterTags);

	// Distance Helpers
	// ====================================================================================================

	/** Get the fixed-point distance between two entities */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Distance Between"))
	static FFixedPoint SeinGetDistanceBetween(const UObject* WorldContextObject, FSeinEntityHandle EntityA, FSeinEntityHandle EntityB);

	/** Get the direction vector from one entity to another */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Direction To"))
	static FFixedVector SeinGetDirectionTo(const UObject* WorldContextObject, FSeinEntityHandle FromEntity, FSeinEntityHandle ToEntity);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
