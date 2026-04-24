/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinVisionData payload
 *          into deterministic sim storage at spawn time. Mirrors the
 *          USeinMovementComponent pattern — pure data, no logic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinVisionData.h"
#include "SeinVisionComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Vision Component"))
class SEINARTSFOGOFWAR_API USeinVisionComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Vision", meta = (ShowOnlyInnerProperties))
	FSeinVisionData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
