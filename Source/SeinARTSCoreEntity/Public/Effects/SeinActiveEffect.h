/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffect.h
 * @brief   Runtime state for a single active effect instance on an entity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Effects/SeinEffectDefinition.h"
#include "SeinActiveEffect.generated.h"

/**
 * Tracks the runtime state of one effect instance that is currently applied
 * to an entity. Created when an effect is applied, removed when it expires
 * or is explicitly removed.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinActiveEffect
{
	GENERATED_BODY()

	/** Unique ID for this effect instance (assigned by FSeinActiveEffectsComponent). */
	UPROPERTY()
	uint32 EffectInstanceID = 0;

	/** The effect definition (contains modifiers, tags, duration settings). */
	UPROPERTY()
	FSeinEffectDefinition Definition;

	/** Remaining duration in simulation seconds (Timed effects only). */
	UPROPERTY()
	FFixedPoint RemainingDuration;

	/** Accumulated time since the last periodic tick. */
	UPROPERTY()
	FFixedPoint TimeSinceLastPeriodic;

	/** Current stack count. */
	UPROPERTY()
	int32 CurrentStacks = 1;

	/** Entity that applied this effect. */
	UPROPERTY()
	FSeinEntityHandle Source;

	/** Entity this effect is applied to. */
	UPROPERTY()
	FSeinEntityHandle Target;
};

FORCEINLINE uint32 GetTypeHash(const FSeinActiveEffect& Effect)
{
	uint32 Hash = GetTypeHash(Effect.EffectInstanceID);
	Hash = HashCombine(Hash, GetTypeHash(Effect.RemainingDuration));
	Hash = HashCombine(Hash, GetTypeHash(Effect.CurrentStacks));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Source));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Target));
	return Hash;
}
