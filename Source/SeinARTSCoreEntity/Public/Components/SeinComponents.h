/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinComponents.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Standard component types for simulation entities.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/EntityID.h"
#include "SeinComponents.generated.h"

/**
 * Movement component for entities that can move.
 * Contains movement parameters and target location.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinMovementComponent : public FTableRowBase
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

	FSeinMovementComponent()
		: MoveSpeed(FFixedPoint::FromInt(5))
		, TargetLocation(FFixedVector::ZeroVector)
		, bHasTarget(false)
		, Acceleration(FFixedPoint::FromInt(10))
		, TurnRate(FFixedPoint::FromFloat(3.14159f))
	{}
};

/** Hash function for hashing */
FORCEINLINE uint32 GetTypeHash(const FSeinMovementComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.MoveSpeed);
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(Component.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(Component.Acceleration));
	Hash = HashCombine(Hash, GetTypeHash(Component.TurnRate));
	return Hash;
}

/**
 * Combat component for entities that can attack.
 * Contains attack parameters and targeting info.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCombatComponent : public FTableRowBase
{
	GENERATED_BODY()

	/** Damage dealt per attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FFixedPoint AttackDamage;

	/** Attack range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FFixedPoint AttackRange;

	/** Time between attacks (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FFixedPoint AttackCooldown;

	/** Current cooldown remaining */
	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	FFixedPoint CurrentCooldown;

	/** Current attack target (if any) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	FSeinID TargetEntity;

	FSeinCombatComponent()
		: AttackDamage(FFixedPoint::FromInt(10))
		, AttackRange(FFixedPoint::FromInt(2))
		, AttackCooldown(FFixedPoint::FromFloat(1.0f))
		, CurrentCooldown(FFixedPoint::Zero)
		, TargetEntity(FSeinID::Invalid())
	{}
};

/** Hash function for hashing */
FORCEINLINE uint32 GetTypeHash(const FSeinCombatComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.AttackDamage);
	Hash = HashCombine(Hash, GetTypeHash(Component.AttackRange));
	Hash = HashCombine(Hash, GetTypeHash(Component.AttackCooldown));
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentCooldown));
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetEntity));
	return Hash;
}
