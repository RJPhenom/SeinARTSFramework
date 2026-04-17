/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceIncomeComponent.h
 * @brief   Component for entities that generate resources over time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Components/SeinComponent.h"
#include "SeinResourceIncomeComponent.generated.h"

/**
 * Resource income component for entities that passively generate resources.
 * Used by captured resource points, economic buildings, etc.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinResourceIncomeComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Resource income rates keyed by resource name (e.g. "Gold", "Wood") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	TMap<FName, FFixedPoint> IncomePerSecond;
};

FORCEINLINE uint32 GetTypeHash(const FSeinResourceIncomeComponent& Component)
{
	uint32 Hash = 0;
	for (const auto& Pair : Component.IncomePerSecond)
	{
		Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
		Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
	}
	return Hash;
}
