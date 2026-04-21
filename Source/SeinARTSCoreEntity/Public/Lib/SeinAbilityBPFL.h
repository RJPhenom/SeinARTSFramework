/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbilityBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for the ability system.
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
#include "Abilities/SeinAbilityTypes.h"
#include "Components/SeinAbilityData.h"
#include "SeinAbilityBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Ability Library"))
class SEINARTSCOREENTITY_API USeinAbilityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	/** Read FSeinAbilityData for an entity. Returns false and logs a warning if the handle
	 *  is invalid or the entity lacks the component; OutData is untouched on failure. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Data"))
	static bool SeinGetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinAbilityData& OutData);

	/** Batch read FSeinAbilityData. Invalid/missing entities are skipped (warning logged); the
	 *  returned array may be shorter than the input. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Data"))
	static TArray<FSeinAbilityData> SeinGetAbilityDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	// Command
	// ====================================================================================================

	/** Activate an ability on an entity by tag, with optional target entity and location */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Activate Ability"))
	static void SeinActivateAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation);

	/** Cancel the currently active ability on an entity */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Cancel Ability"))
	static void SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether a specific ability is on cooldown */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Ability On Cooldown"))
	static bool SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Get the remaining cooldown time for a specific ability */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cooldown Remaining"))
	static FFixedPoint SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Check whether an entity has an ability with the given tag */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Ability"))
	static bool SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	// Availability
	// ====================================================================================================

	/** Aggregate availability snapshot for one ability on one entity. Matches the
	 *  shape of USeinProductionBPFL::SeinGetProductionAvailability for uniform UI
	 *  handling. Walks the same gates as ProcessCommands::ActivateAbility (cooldown
	 *  → blocked-tags → range / valid-target / LOS → CanActivate → affordability)
	 *  and reports the first failing gate in the Reason field. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Availability"))
	static FSeinAbilityAvailability SeinGetAbilityAvailability(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FGameplayTag AbilityTag,
		FSeinEntityHandle OptionalTargetEntity,
		FFixedVector OptionalTargetLocation);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
