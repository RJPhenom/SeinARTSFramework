/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementBPFL.h
 * @brief   Blueprint Function Library for movement component reads.
 *          Writes are in USeinSimMutationBPFL (restricted to ability/effect graphs).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinMovementData.h"
#include "SeinMovementBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Movement Library"))
class SEINARTSCOREENTITY_API USeinMovementBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Read FSeinMovementData for an entity. Returns false on invalid handle / missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Movement Data"))
	static bool SeinGetMovementData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinMovementData& OutData);

	/** Batch read FSeinMovementData. Invalid/missing entities are skipped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Movement Data"))
	static TArray<FSeinMovementData> SeinGetMovementDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
