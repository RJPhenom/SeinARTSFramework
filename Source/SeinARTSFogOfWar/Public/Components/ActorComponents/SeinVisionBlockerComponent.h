/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionBlockerComponent.h
 * @brief   UActorComponent wrapper that carries an FSeinVisionBlockerData
 *          payload into deterministic sim storage at spawn time. Dynamic
 *          world-space vision blockers (smoke grenades, destructibles)
 *          register via this component; static blockers (walls, buildings)
 *          bake into the fog grid at level load and don't need it.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/SeinVisionBlockerData.h"
#include "SeinVisionBlockerComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Vision Blocker Component"))
class SEINARTSFOGOFWAR_API USeinVisionBlockerComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Vision", meta = (ShowOnlyInnerProperties))
	FSeinVisionBlockerData Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return FInstancedStruct::Make(Data);
	}
};
