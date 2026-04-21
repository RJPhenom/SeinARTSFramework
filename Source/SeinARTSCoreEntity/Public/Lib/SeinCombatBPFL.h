/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinCombatBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Thin combat BPFL per DESIGN §11. The framework does NOT compute
 *				damage math — callers pass the final amount. This library
 *				writes the `FSeinCombatData.Health` delta, fires visual events,
 *				bumps attribution stats, and triggers the death-handler flow.
 *				Damage-type vocabulary, armor, accuracy models, and weapons
 *				are designer-owned (see DESIGN §11).
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
#include "Components/SeinCombatData.h"
#include "SeinCombatBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Combat Library"))
class SEINARTSCOREENTITY_API USeinCombatBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Combat Data"))
	static bool SeinGetCombatData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinCombatData& OutData);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Combat Data"))
	static TArray<FSeinCombatData> SeinGetCombatDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	// Damage / Heal
	// ====================================================================================================

	/** Apply a pre-computed damage amount to the target's Health. Does NOT compute
	 *  armor, damage-type mitigation, or accuracy — caller hands in the final
	 *  number. Framework writes the delta, fires `DamageApplied`, bumps
	 *  `Stat.TotalDamageDealt` on the source's player + `Stat.TotalDamageReceived`
	 *  on the target's player, and triggers the death-handler flow if Health <= 0.
	 *  Pass a valid SourceHandle so attribution + kill-feed stats resolve; pass an
	 *  invalid handle for world-source damage (no attribution). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Damage"))
	static void SeinApplyDamage(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FGameplayTag DamageType, FSeinEntityHandle SourceHandle);

	/** Apply a pre-computed heal amount to the target's Health, clamped to MaxHealth.
	 *  Fires `HealApplied` and bumps `Stat.TotalHealsDealt` on the source's player. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Heal"))
	static void SeinApplyHeal(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FGameplayTag HealType, FSeinEntityHandle SourceHandle);

	/** Convenience spatial loop: query entities in a sphere around `Center`, then
	 *  apply `Amount` damage to each (linearly falling off by `Falloff` in [0,1]
	 *  from center to edge — 0 = flat, 1 = reaches zero at the edge). The framework
	 *  still performs no damage math beyond the falloff interpolation; caller is
	 *  responsible for damage type routing, friendly-fire checks, etc. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Splash Damage"))
	static void SeinApplySplashDamage(const UObject* WorldContextObject, FFixedVector Center, FFixedPoint Radius, FFixedPoint Amount, FFixedPoint Falloff, FGameplayTag DamageType, FSeinEntityHandle SourceHandle);

	// Generic Mutation (escape hatch)
	// ====================================================================================================

	/** Write a `Delta` (positive or negative) to a fixed-point field on an arbitrary
	 *  component. No visual events, no stat bumps — designer composes their own
	 *  path. Use for custom combat models that don't fit the Health/MaxHealth pattern. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Mutate Attribute"))
	static bool SeinMutateAttribute(const UObject* WorldContextObject, FSeinEntityHandle Target, UScriptStruct* ComponentStruct, FName FieldName, FFixedPoint Delta);

	// PRNG Convenience
	// ====================================================================================================

	/** Deterministic accuracy roll. `BaseAccuracy` in [0,1] — returns true with that
	 *  probability using the sim-owned PRNG. Safe across all clients / replays.
	 *  Optional convenience — games that prefer deterministic-average damage can skip it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Roll Accuracy"))
	static bool SeinRollAccuracy(const UObject* WorldContextObject, FFixedPoint BaseAccuracy);

	// Spatial / Distance helpers (pre-existing)
	// ====================================================================================================

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entities In Range"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInRange(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Nearest Entity"))
	static FSeinEntityHandle SeinGetNearestEntity(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entities In Box"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInBox(const UObject* WorldContextObject, FFixedVector Min, FFixedVector Max, FGameplayTagContainer FilterTags);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Distance Between"))
	static FFixedPoint SeinGetDistanceBetween(const UObject* WorldContextObject, FSeinEntityHandle EntityA, FSeinEntityHandle EntityB);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Direction To"))
	static FFixedVector SeinGetDirectionTo(const UObject* WorldContextObject, FSeinEntityHandle FromEntity, FSeinEntityHandle ToEntity);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
