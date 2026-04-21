/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWeaponProfile.h
 * @brief   Starter weapon-profile component template. Per DESIGN §11 the
 *          framework never reads this struct — it's shipped as a starting
 *          point for designers who want a conventional weapon shape. Extend
 *          with additional components (ammo, reload time, barrel count),
 *          replace entirely, or skip the primitive and hardcode damage in
 *          attack abilities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "SeinWeaponProfile.generated.h"

/**
 * Designer-template weapon profile. No framework code reads this struct.
 * Attach multiple weapon-profile components to a unit for multi-weapon patterns;
 * the attack ability iterates them and spawns per-weapon latent actions.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinWeaponProfile : public FSeinComponent
{
	GENERATED_BODY()

	/** Damage-type tag — designers define their own vocabulary under DamageType.*;
	 *  framework ships `SeinARTS.DamageType.Default` as a starter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon",
		meta = (Categories = "SeinARTS.DamageType"))
	FGameplayTag DamageType;

	/** Base damage per hit. Attack abilities route through USeinCombatBPFL::SeinApplyDamage
	 *  with whatever math (armor, modifiers) the designer wants on top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon")
	FFixedPoint BaseDamage;

	/** Maximum engagement range in sim units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon")
	FFixedPoint Range;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon")
	FFixedPoint FireInterval;

	/** Accuracy in [0,1] — pipe through USeinCombatBPFL::SeinRollAccuracy if you
	 *  want PRNG-backed hit/miss; skip for deterministic-average damage models. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon")
	FFixedPoint AccuracyBase;

	/** Arbitrary designer tag — pattern-match in abilities / UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Weapon")
	FGameplayTag WeaponTag;
};
