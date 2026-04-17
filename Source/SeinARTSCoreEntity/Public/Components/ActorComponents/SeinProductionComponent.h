/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinProductionData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinProductionData.h"
#include "SeinProductionComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Production Component"))
class SEINARTSCOREENTITY_API USeinProductionComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinProductionData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
