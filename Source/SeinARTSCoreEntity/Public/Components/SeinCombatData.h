/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinCombatData.h
 * @brief:   Sim-side combat component data. Deterministic, stored in ECS
 *           storage; the USeinCombatComponent UActorComponent carries this
 *           struct as its sim payload.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "Types/EntityID.h"
#include "SeinCombatData.generated.h"

USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCombatData : public FSeinComponent
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

	FSeinCombatData()
		: AttackDamage(FFixedPoint::FromInt(10))
		, AttackRange(FFixedPoint::FromInt(2))
		, AttackCooldown(FFixedPoint::FromFloat(1.0f))
		, CurrentCooldown(FFixedPoint::Zero)
		, TargetEntity(FSeinID::Invalid())
	{}
};

FORCEINLINE uint32 GetTypeHash(const FSeinCombatData& Component)
{
	uint32 Hash = GetTypeHash(Component.AttackDamage);
	Hash = HashCombine(Hash, GetTypeHash(Component.AttackRange));
	Hash = HashCombine(Hash, GetTypeHash(Component.AttackCooldown));
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentCooldown));
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetEntity));
	return Hash;
}
