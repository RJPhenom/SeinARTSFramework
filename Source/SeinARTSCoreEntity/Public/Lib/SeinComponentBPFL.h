/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentBPFL.h
 * @brief   Generic component access — escape hatch for designer-authored
 *          dynamic components (USeinStructComponent + UDS) and introspection.
 *          Per-component typed BPFLs (SeinCombatBPFL, SeinMovementBPFL, ...)
 *          are the preferred path for known component types.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinComponentBPFL.generated.h"

class USeinWorldSubsystem;
class USeinActorComponent;

UCLASS(meta = (DisplayName = "SeinARTS Component Library"))
class SEINARTSCOREENTITY_API USeinComponentBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Read a component's data as FInstancedStruct, keyed by struct type. Returns false
	 *  and logs a warning on invalid handle / missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Component Data"))
	static bool SeinGetComponentData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData);

	/** Read a component's data AND look up the associated render-side UActorComponent
	 *  on the bridged actor, if any. OutActorComponent is null for abstract entities
	 *  (no render actor) or when no AC on the actor resolves to this struct type. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Component"))
	static bool SeinGetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData, USeinActorComponent*& OutActorComponent);

	/** List every component on the entity as FInstancedStruct. Iterates every registered
	 *  storage. Useful for debug tooling and generic designer introspection. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Components"))
	static TArray<FInstancedStruct> SeinGetComponents(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
