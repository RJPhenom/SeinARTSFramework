/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinExtentsData payload
 *          into deterministic sim storage at spawn time. The unified "where
 *          is this entity in space" component — used by FoW visibility-as-
 *          target, future combat hit detection, and (Phase 2) nav + vision
 *          blocking.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinExtentsData.h"
#include "SeinExtentsComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Extents Component"))
class SEINARTSCOREENTITY_API USeinExtentsComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents", meta = (ShowOnlyInnerProperties))
	FSeinExtentsData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
