/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinActiveEffectsData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinActiveEffectsData.h"
#include "SeinActiveEffectsComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Active Effects Component"))
class SEINARTSCOREENTITY_API USeinActiveEffectsComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Effect", meta = (ShowOnlyInnerProperties))
	FSeinActiveEffectsData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
