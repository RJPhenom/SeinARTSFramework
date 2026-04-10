/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectDefinition.h
 * @brief   Data-driven effect definition: a timed or permanent bundle of modifiers.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Attributes/SeinModifier.h"
#include "GameplayTagContainer.h"
#include "SeinEffectDefinition.generated.h"

/** How long an effect persists. */
UENUM(BlueprintType)
enum class ESeinEffectDuration : uint8
{
	Instant,    // Apply modifiers once, then remove the effect
	Timed,      // Lasts for Duration simulation seconds
	Infinite    // Remains until explicitly removed
};

/** How re-application of the same effect is handled. */
UENUM(BlueprintType)
enum class ESeinEffectStacking : uint8
{
	None,               // Re-application refreshes duration (no stacking)
	DurationRefresh,    // Explicit alias for None
	StackCount,         // Stacks up to MaxStacks times
	UniqueSource        // Stacks only from different source entities
};

/**
 * Immutable definition of an effect. Designed to be authored in data assets
 * or Blueprints and shared across many active effect instances.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinEffectDefinition
{
	GENERATED_BODY()

	/** Human-readable name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FName EffectName;

	/** Gameplay tag that uniquely identifies this effect type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FGameplayTag EffectTag;

	/** Duration behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Duration")
	ESeinEffectDuration DurationType = ESeinEffectDuration::Instant;

	/** Duration in simulation seconds (only used when DurationType == Timed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Duration", meta = (EditCondition = "DurationType == ESeinEffectDuration::Timed"))
	FFixedPoint Duration;

	/** Modifiers applied while this effect is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifiers")
	TArray<FSeinModifier> Modifiers;

	/** Stacking policy when the same effect is applied again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stacking")
	ESeinEffectStacking StackingPolicy = ESeinEffectStacking::None;

	/** Maximum number of stacks (only for StackCount / UniqueSource). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stacking", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** Whether this effect ticks periodically (e.g. damage-over-time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Periodic")
	bool bPeriodic = false;

	/** Seconds between periodic ticks (only when bPeriodic is true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Periodic", meta = (EditCondition = "bPeriodic"))
	FFixedPoint Period;

	/** Tags granted to the target entity while this effect is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	FGameplayTagContainer GrantedTags;

	/** When this effect is applied, remove any active effects whose EffectTag matches these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	FGameplayTagContainer RemoveEffectsWithTags;
};
