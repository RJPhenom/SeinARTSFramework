/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilitiesComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinAbilityData
 *          payload into deterministic sim storage at spawn time.
 *          Plural: one component holds the unit's full set of granted abilities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinAbilityData.h"
#include "SeinAbilitiesComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Abilities Component"))
class SEINARTSCOREENTITY_API USeinAbilitiesComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinAbilityData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
