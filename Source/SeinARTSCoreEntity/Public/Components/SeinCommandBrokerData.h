/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerData.h
 * @brief   Sim-component carrying CommandBroker state (DESIGN §5). Lives on
 *          the abstract broker entity; holds member list, resolver reference,
 *          centroid/anchor, cached capability map, and the order queue.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Components/SeinComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinCommandBrokerData.generated.h"

class USeinCommandBrokerResolver;

/** Capability-map value wrapper — `TMap<FGameplayTag, TArray<FSeinEntityHandle>>`
 *  isn't allowed as a raw UPROPERTY map. Wrapping the array in a USTRUCT
 *  sidesteps UHT's "nested containers" restriction the same way
 *  `FSeinCellTagOverflowEntry` does in the nav module. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerCapabilityBucket
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> Members;
};

/**
 * Sim-component for command broker entities (DESIGN §5).
 *
 * A broker wraps a set of entities (all owned by the same FSeinPlayerID) as a
 * single dispatch target. Member dispatch is delegated to a designer-pluggable
 * `USeinCommandBrokerResolver`, which consumes the capability map + order
 * context and returns per-member (ability, target) tuples the system issues
 * internally on tick.
 *
 * Not authored on a Blueprint — created on demand by
 * `USeinWorldSubsystem::ProcessCommands` when a `BrokerOrder` command arrives.
 * Culled by `FSeinCommandBrokerSystem` when the member list and order queue
 * both empty.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCommandBrokerData : public FSeinComponent
{
	GENERATED_BODY()

	/** Current member list. All members must share the broker's owning player.
	 *  `FSeinCommandBrokerSystem` strips dead handles each tick. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> Members;

	/** Resolver instance. Populated when the broker spawns, from
	 *  `USeinARTSCoreSettings::DefaultBrokerResolverClass` (or the C++ default
	 *  `USeinDefaultCommandBrokerResolver` if unset). */
	UPROPERTY()
	TObjectPtr<USeinCommandBrokerResolver> Resolver;

	/** Dynamic centroid of the live member set (updated each tick). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedVector Centroid;

	/** "Where the formation is trying to stand" — stamped when an order
	 *  dispatches. Used by tight-formation resolvers + UI banners. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedVector Anchor;

	/** Facing associated with Anchor. Zero-rotation = unset. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FFixedQuaternion AnchorFacing;

	/** Cached "which members can service each ability tag." Rebuilt from
	 *  members' FSeinAbilityData when `bCapabilityMapDirty` is true. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TMap<FGameplayTag, FSeinBrokerCapabilityBucket> CapabilityMap;

	/** Set on member add/remove. Cleared after the next `RebuildCapabilityMap`. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bCapabilityMapDirty = true;

	/** True while an order is in flight (dispatch issued, waiting for members
	 *  to complete). Flipped off by the system when completion predicate passes. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	bool bIsExecuting = false;

	/** Click context of the currently-executing order (empty if !bIsExecuting).
	 *  Container, not a single tag — resolvers get the raw player intent and
	 *  interpret per-member. UI / diagnostics can read e.g. a dominant tag
	 *  out of this for display. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FGameplayTagContainer CurrentOrderContext;

	/** FIFO queue of shift-chained orders. The front entry drives dispatch
	 *  when `bIsExecuting` is false. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	TArray<FSeinBrokerQueuedOrder> OrderQueue;
};

FORCEINLINE uint32 GetTypeHash(const FSeinCommandBrokerData& Data)
{
	uint32 Hash = GetTypeHash(Data.bIsExecuting);
	Hash = HashCombine(Hash, GetTypeHash(Data.Members.Num()));
	Hash = HashCombine(Hash, GetTypeHash(Data.OrderQueue.Num()));
	Hash = HashCombine(Hash, GetTypeHash(Data.Centroid));
	return Hash;
}
