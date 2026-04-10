/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinTagBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for gameplay tag queries on entities.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "SeinTagBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Tag Library"))
class SEINARTSCOREENTITY_API USeinTagBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Check whether an entity has a specific gameplay tag */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Has Tag"))
	static bool SeinHasTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** Check whether an entity has any of the specified tags */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Has Any Tag"))
	static bool SeinHasAnyTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags);

	/** Check whether an entity has all of the specified tags */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Has All Tags"))
	static bool SeinHasAllTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags);

	/** Add a tag to an entity's base tags */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Add Tag"))
	static void SeinAddTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** Remove a tag from an entity's base tags */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Remove Tag"))
	static void SeinRemoveTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
