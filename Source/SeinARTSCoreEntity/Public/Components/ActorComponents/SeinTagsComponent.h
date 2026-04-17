/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagsComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinTagData
 *          payload into deterministic sim storage at spawn time.
 *          Plural: one component carries the unit's full tag container.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinTagData.h"
#include "SeinTagsComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Tags Component"))
class SEINARTSCOREENTITY_API USeinTagsComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinTagData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
