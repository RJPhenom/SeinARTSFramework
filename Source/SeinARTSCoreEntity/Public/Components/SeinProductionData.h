/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionData.h
 * @brief   Single-queue production component for buildings.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "Actor/SeinActor.h"
#include "Attributes/SeinModifier.h"
#include "Components/SeinComponent.h"
#include "SeinProductionData.generated.h"

/**
 * A single entry in a production queue.
 * Stores a snapshot of cost at queue time so refunds are deterministic.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinProductionQueueEntry
{
	GENERATED_BODY()

	/** The Blueprint class of the entity being produced */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	TSubclassOf<ASeinActor> ActorClass;

	/** Total time required to produce this entry */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	FFixedPoint TotalBuildTime;

	/** Snapshot of resource cost at queue time (used for refund on cancel) */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	TMap<FName, FFixedPoint> Cost;

	/** If true, this entry is a research item (grants tech instead of spawning a unit). */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	bool bIsResearch = false;

	/** Tech tag granted when research completes (only used if bIsResearch). */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	FGameplayTag ResearchTechTag;

	/** Archetype modifiers granted when research completes (only used if bIsResearch). */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	TArray<FSeinModifier> ResearchModifiers;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionQueueEntry& Entry)
{
	uint32 Hash = GetTypeHash(Entry.TotalBuildTime);
	Hash = HashCombine(Hash, GetTypeHash(Entry.ActorClass.Get()));
	for (const auto& Pair : Entry.Cost)
	{
		Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
		Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
	}
	return Hash;
}

/**
 * Production component for buildings that can produce units.
 * Maintains a single ordered queue with configurable max size.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinProductionData : public FSeinComponent
{
	GENERATED_BODY()

	/** Blueprint classes this building can produce */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	TArray<TSubclassOf<ASeinActor>> ProducibleClasses;

	/** Maximum number of items allowed in the queue */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	int32 MaxQueueSize = 5;

	/** Current production queue */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	TArray<FSeinProductionQueueEntry> Queue;

	/** Build progress of the current (front) queue entry */
	UPROPERTY(BlueprintReadOnly, Category = "Production")
	FFixedPoint CurrentBuildProgress;

	/** Rally point for produced units */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	FFixedVector RallyPoint;

	/** Returns true if at least one item is in the queue */
	bool IsProducing() const;

	/** Returns 0-1 progress fraction for the current item. Returns Zero if not producing. */
	FFixedPoint GetProgressPercent() const;

	/** Returns true if the queue has room for another entry */
	bool CanQueueMore() const;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionData& Component)
{
	uint32 Hash = GetTypeHash(Component.MaxQueueSize);
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentBuildProgress));
	Hash = HashCombine(Hash, GetTypeHash(Component.RallyPoint));
	for (const auto& Entry : Component.Queue)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry));
	}
	return Hash;
}
