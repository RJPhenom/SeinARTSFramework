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
#include "UObject/SoftObjectPath.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMovementData : public FSeinComponent
{
	GENERATED_BODY()

	/** Movement speed in units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint MoveSpeed;

	/** Target location to move toward */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector TargetLocation;

	/** Whether entity currently has a move target */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bHasTarget;

	/** Acceleration rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint Acceleration;

	/** Turn rate in radians per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint TurnRate;

	/** Locomotion class — how the entity advances along a nav path (seek+arrive,
	 *  turn-in-place tracked, turn-radius wheeled, etc.). Soft class path (not
	 *  TSubclassOf) because the concrete USeinLocomotion subclasses live in
	 *  SeinARTSNavigation, which depends on this module; a direct TSubclassOf
	 *  would flip the dep and create a cycle. Resolved to a UClass* at
	 *  action-init time via TryLoadClass.
	 *
	 *  Null / invalid defaults to USeinBasicMovement (simple seek + arrive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (DisplayName = "Locomotion Class",
				MetaClass = "/Script/SeinARTSNavigation.SeinLocomotion"))
	FSoftClassPath LocomotionClass;

	FSeinMovementData()
		: MoveSpeed(FFixedPoint::FromInt(5))
		, TargetLocation(FFixedVector::ZeroVector)
		, bHasTarget(false)
		, Acceleration(FFixedPoint::FromInt(10))
		, TurnRate(FFixedPoint::FromFloat(3.14159f))
		, LocomotionClass(FSoftClassPath(TEXT("/Script/SeinARTSNavigation.SeinBasicMovement")))
	{}
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementData& Component)
{
	uint32 Hash = GetTypeHash(Component.MoveSpeed);
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(Component.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(Component.Acceleration));
	Hash = HashCombine(Hash, GetTypeHash(Component.TurnRate));
	Hash = HashCombine(Hash, GetTypeHash(Component.LocomotionClass));
	return Hash;
}
