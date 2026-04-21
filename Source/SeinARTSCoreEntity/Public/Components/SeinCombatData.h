/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinCombatData.h
 * @brief:   Minimal combat state per DESIGN §11. The framework only owns
 *           Health + MaxHealth; damage types, armor, accuracy, weapons are
 *           designer-authored via extension components and ability scripts.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "SeinCombatData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCombatData : public FSeinComponent
{
	GENERATED_BODY()

	/** Maximum health. Designers author via the archetype; runtime modifiers come
	 *  through the attribute resolver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint MaxHealth;

	/** Current health. Reaches zero → death flow in USeinCombatBPFL::SeinApplyDamage
	 *  activates any ability tagged `SeinARTS.DeathHandler` then destroys the entity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint Health;

	FSeinCombatData()
		: MaxHealth(FFixedPoint::FromInt(100))
		, Health(FFixedPoint::FromInt(100))
	{}
};

FORCEINLINE uint32 GetTypeHash(const FSeinCombatData& Component)
{
	uint32 Hash = GetTypeHash(Component.MaxHealth);
	Hash = HashCombine(Hash, GetTypeHash(Component.Health));
	return Hash;
}
