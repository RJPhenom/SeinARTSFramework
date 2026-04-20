/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceIncomeBPFL.h
 * @brief   Blueprint Function Library for resource-income component reads.
 *          Writes are in USeinSimMutationBPFL (restricted to ability/effect graphs).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinResourceIncomeData.h"
#include "SeinResourceIncomeBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Resource Income Library"))
class SEINARTSCOREENTITY_API USeinResourceIncomeBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Read FSeinResourceIncomeData for an entity. Returns false on invalid handle / missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Resource Income Data"))
	static bool SeinGetResourceIncomeData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinResourceIncomeData& OutData);

	/** Batch read FSeinResourceIncomeData. Invalid/missing entities are skipped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Resource Income Data"))
	static TArray<FSeinResourceIncomeData> SeinGetResourceIncomeDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
