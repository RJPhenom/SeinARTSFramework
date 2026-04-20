/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinMovementData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinMovementData.h"
#include "SeinMovementComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Movement Component"))
class SEINARTSCOREENTITY_API USeinMovementComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement", meta = (ShowOnlyInnerProperties))
	FSeinMovementData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
