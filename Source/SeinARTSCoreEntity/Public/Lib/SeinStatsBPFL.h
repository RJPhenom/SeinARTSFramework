/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStatsBPFL.h
 * @brief   Attribution stats BPFL per DESIGN §11. Tag-keyed counters on
 *          `FSeinPlayerState::StatCounters`. Framework bumps the shipped
 *          `SeinARTS.Stat.*` vocabulary from its combat / production BPFLs;
 *          designers add their own stat tags and call `SeinBumpStat` from
 *          ability graphs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "SeinStatsBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Stats Library"))
class SEINARTSCOREENTITY_API USeinStatsBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Increment (or decrement, for negative amounts) the tagged counter by Amount.
	 *  Creates the entry on first bump. Safe to call with invalid player ID (no-op). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Stats",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Bump Stat"))
	static void SeinBumpStat(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag StatTag, FFixedPoint Amount);

	/** Read the current counter value. Returns zero on unknown player / tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Stats",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Stat"))
	static FFixedPoint SeinGetStat(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag StatTag);

	/** Returns the full stat map for a player. Empty on unknown player. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Stats",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get All Stats"))
	static TMap<FGameplayTag, FFixedPoint> SeinGetAllStats(const UObject* WorldContextObject, FSeinPlayerID PlayerID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
