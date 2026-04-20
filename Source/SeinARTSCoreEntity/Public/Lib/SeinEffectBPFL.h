/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEffectBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for the effect system.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Effects/SeinEffectDefinition.h"
#include "SeinEffectBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Effect Library"))
class SEINARTSCOREENTITY_API USeinEffectBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Apply an effect to a target entity. Returns the effect instance ID for later removal. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect", meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Effect"))
	static int32 SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, const FSeinEffectDefinition& EffectDefinition, FSeinEntityHandle SourceHandle);

	/** Remove a specific effect instance by its ID */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect", meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effect"))
	static void SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int32 EffectInstanceID);

	/** Remove all effects on an entity that match a given tag */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect", meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effects With Tag"))
	static void SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** Check whether an entity has any active effect with a given tag */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Effect With Tag"))
	static bool SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** Get the current stack count of a named effect on an entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Effect Stacks"))
	static int32 SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FName EffectName);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
