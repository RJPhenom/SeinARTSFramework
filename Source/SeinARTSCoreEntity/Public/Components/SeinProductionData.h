/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionData.h
 * @brief   Single-queue production component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "Actor/SeinActor.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "Data/SeinResourceTypes.h"
#include "SeinProductionData.generated.h"

class USeinEffect;

/**
 * Refund policy for a cancelled production queue entry. DESIGN §9 Q2.
 * Default: refund = (1 - progress_fraction) * cost. Opt-in: flat custom percentage.
 * Authored on `USeinArchetypeDefinition`; snapshotted on each queue entry at enqueue.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionRefundPolicy
{
	GENERATED_BODY()

	/** If true, use `CustomRefundPercentage` instead of progress-proportional refund. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bUseCustomRefund = false;

	/** Flat fraction of cost refunded when bUseCustomRefund == true.
	 *  1.0 = full refund, 0.0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production",
		meta = (EditCondition = "bUseCustomRefund", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint CustomRefundPercentage = FFixedPoint::One;
};

/**
 * Rally-target descriptor: an entity handle OR a world location. When
 * `bIsEntityTarget` is set, produced units path to `EntityTarget`; otherwise
 * they path to `Location`. DESIGN §9 Q9.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinRallyTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production")
	bool bIsEntityTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production",
		meta = (EditCondition = "!bIsEntityTarget"))
	FFixedVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production",
		meta = (EditCondition = "bIsEntityTarget"))
	FSeinEntityHandle EntityTarget;
};

FORCEINLINE uint32 GetTypeHash(const FSeinRallyTarget& R)
{
	uint32 Hash = GetTypeHash(R.bIsEntityTarget);
	Hash = HashCombine(Hash, GetTypeHash(R.Location));
	Hash = HashCombine(Hash, GetTypeHash(R.EntityTarget));
	return Hash;
}

/**
 * One entry in a production queue. Carries a snapshot of cost at queue time so
 * refunds are deterministic, plus — for research entries — the USeinEffect
 * class to apply on completion (per DESIGN §10 tech unification).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionQueueEntry
{
	GENERATED_BODY()

	/** Blueprint class being produced. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TSubclassOf<ASeinActor> ActorClass;

	/** Total time required to complete. Snapshot — not affected by mid-build
	 *  modifiers per DESIGN §9 Q4b. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FFixedPoint TotalBuildTime;

	/** Cost portion that was deducted at enqueue time (resources whose catalog
	 *  `ProductionDeductionTiming == AtEnqueue`). Drives the refund on cancel. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinResourceCost AtEnqueueCost;

	/** Cost portion deferred to completion time (resources whose catalog
	 *  `ProductionDeductionTiming == AtCompletion`). Attempted on spawn; may
	 *  stall if it would exceed cap. Never refunded on cancel (never deducted). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinResourceCost AtCompletionCost;

	/** Research entries: USeinEffect class applied to the owner on completion. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bIsResearch = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TSubclassOf<USeinEffect> ResearchEffectClass;

	/** Copy of the owning archetype's refund policy, frozen at enqueue. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinProductionRefundPolicy RefundPolicy;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionQueueEntry& Entry)
{
	uint32 Hash = GetTypeHash(Entry.TotalBuildTime);
	Hash = HashCombine(Hash, GetTypeHash(Entry.ActorClass.Get()));
	Hash = HashCombine(Hash, GetTypeHash(Entry.AtEnqueueCost));
	Hash = HashCombine(Hash, GetTypeHash(Entry.AtCompletionCost));
	return Hash;
}

/**
 * Production component for entities that can produce units or research. Any
 * entity carrying this can produce — not limited to buildings per DESIGN §9.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionData : public FSeinComponent
{
	GENERATED_BODY()

	/** Blueprint classes this entity can produce. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production")
	TArray<TSubclassOf<ASeinActor>> ProducibleClasses;

	/** Maximum queue depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production")
	int32 MaxQueueSize = 5;

	/** Current production queue. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TArray<FSeinProductionQueueEntry> Queue;

	/** Build progress of the current (front) queue entry. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FFixedPoint CurrentBuildProgress;

	/** True iff the front item reached 100% but the AtCompletion cost wouldn't
	 *  fit (e.g., pop cap hit). Cleared once AttemptSpawn succeeds. DESIGN §9 stall-at-completion. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bStalledAtCompletion = false;

	/** Rally target (location or entity) for produced units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production")
	FSeinRallyTarget RallyTarget;

	bool IsProducing() const;
	FFixedPoint GetProgressPercent() const;
	bool CanQueueMore() const;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionData& Component)
{
	uint32 Hash = GetTypeHash(Component.MaxQueueSize);
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentBuildProgress));
	Hash = HashCombine(Hash, GetTypeHash(Component.RallyTarget));
	Hash = HashCombine(Hash, GetTypeHash(Component.bStalledAtCompletion));
	for (const auto& Entry : Component.Queue)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry));
	}
	return Hash;
}
