/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadMemberComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinSquadMemberData
 *          payload into deterministic sim storage at spawn time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinSquadMemberData.h"
#include "SeinSquadMemberComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Squad Member Component"))
class SEINARTSCOREENTITY_API USeinSquadMemberComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad", meta = (ShowOnlyInnerProperties))
	FSeinSquadMemberData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
