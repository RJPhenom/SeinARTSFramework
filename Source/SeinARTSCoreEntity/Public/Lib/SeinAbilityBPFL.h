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
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinAbilityBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Ability Library"))
class SEINARTSCOREENTITY_API USeinAbilityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Activate an ability on an entity by tag, with optional target entity and location */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Abilities", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Activate Ability"))
	static void SeinActivateAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation);

	/** Cancel the currently active ability on an entity */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Abilities", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Cancel Ability"))
	static void SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether a specific ability is on cooldown */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Abilities", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Is Ability On Cooldown"))
	static bool SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Get the remaining cooldown time for a specific ability */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Abilities", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Cooldown Remaining"))
	static FFixedPoint SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Check whether an entity has an ability with the given tag */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Abilities", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Has Ability"))
	static bool SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
