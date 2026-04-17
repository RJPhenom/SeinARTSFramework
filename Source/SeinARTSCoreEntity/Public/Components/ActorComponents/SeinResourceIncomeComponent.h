/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceIncomeComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinResourceIncomeData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinResourceIncomeData.h"
#include "SeinResourceIncomeComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Resource Income Component"))
class SEINARTSCOREENTITY_API USeinResourceIncomeComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinResourceIncomeData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
