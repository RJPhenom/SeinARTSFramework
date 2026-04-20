/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStructComponent.h
 * @brief   Generic non-Blueprintable wrapper AC that carries a designer-
 *          authored UDS payload into deterministic sim storage at spawn.
 *          Composes via UE's native asset-to-component broker pattern —
 *          drag a Sein-marked UDS from the Content Browser onto a SeinActor
 *          BP's components panel; the broker typed the Data field by the
 *          dragged UDS. See DESIGN.md §2 Components.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinStructComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Struct Component"))
class SEINARTSCOREENTITY_API USeinStructComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	/** Designer-authored sim payload. Typed by the source UDS at broker-assign time.
	 *  The `SeinDeterministicOnly` meta narrows the struct-type picker to Sein-marked
	 *  structs (see FSeinInstancedStructFilter) so non-Sein UDSes don't surface here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Component",
		meta = (ShowOnlyInnerProperties, SeinDeterministicOnly))
	FInstancedStruct Data;

	virtual FInstancedStruct GetSimComponent() const override
	{
		return Data;
	}
};
