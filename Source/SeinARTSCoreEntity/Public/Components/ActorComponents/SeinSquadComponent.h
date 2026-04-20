/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinSquadData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinSquadData.h"
#include "SeinSquadComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Squad Component"))
class SEINARTSCOREENTITY_API USeinSquadComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad", meta = (ShowOnlyInnerProperties))
	FSeinSquadData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
