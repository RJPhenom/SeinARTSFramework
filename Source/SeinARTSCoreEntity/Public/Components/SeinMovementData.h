/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinMovementData.h
 * @brief:   Sim-side movement component data. Deterministic, stored in ECS
 *           storage; the USeinMovementComponent UActorComponent carries this
 *           struct as its sim payload.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMovementData.generated.h"

USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinMovementData : public FSeinComponent
{
	GENERATED_BODY()

	/** Movement speed in units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FFixedPoint MoveSpeed;

	/** Target location to move toward */
	UPROPERTY(BlueprintReadWrite, Category = "Movement")
	FFixedVector TargetLocation;

	/** Whether entity currently has a move target */
	UPROPERTY(BlueprintReadWrite, Category = "Movement")
	bool bHasTarget;

	/** Acceleration rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FFixedPoint Acceleration;

	/** Turn rate in radians per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FFixedPoint TurnRate;

	FSeinMovementData()
		: MoveSpeed(FFixedPoint::FromInt(5))
		, TargetLocation(FFixedVector::ZeroVector)
		, bHasTarget(false)
		, Acceleration(FFixedPoint::FromInt(10))
		, TurnRate(FFixedPoint::FromFloat(3.14159f))
	{}
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementData& Component)
{
	uint32 Hash = GetTypeHash(Component.MoveSpeed);
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(Component.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(Component.Acceleration));
	Hash = HashCombine(Hash, GetTypeHash(Component.TurnRate));
	return Hash;
}
