/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBrokerTypes.h
 * @brief   Shared types for the CommandBroker primitive (DESIGN §5):
 *          - FSeinBrokerQueuedOrder: one order sitting in a broker's queue
 *          - FSeinBrokerOrderInput : resolver input packet
 *          - FSeinBrokerMemberDispatch / FSeinBrokerDispatchPlan : resolver output
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "SeinBrokerTypes.generated.h"

/**
 * One queued order on a broker. Shift-chained dispatches append to
 * FSeinCommandBrokerData::OrderQueue; each is consumed in FIFO order as the
 * previous completes.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerQueuedOrder
{
	GENERATED_BODY()

	/** Raw click context — RightClick + Target.Ground/Friendly/Enemy/Neutral plus
	 *  any designer-added target tags. Resolved sim-side by the broker's resolver
	 *  per-member (DESIGN §5). Replaces the pre-resolved AbilityTag field: the
	 *  player controller emits one pre-resolution command, the broker interprets. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTagContainer Context;

	/** Optional target entity (attack target, repair target, etc.). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	/** Target world location (move destination, attack point, rally, …). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	/** Optional second endpoint for drag orders (formation line end). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector FormationEnd;

	/** Subset of the broker's members this order dispatches against. Empty = all
	 *  members. Used by (a) player shift-click on a subset of a shared broker —
	 *  the new order is appended to the broker's queue but targets only the
	 *  selected subset, and (b) framework-injected prefixes (e.g. AutoMoveThen's
	 *  Move prefix for a single out-of-range member). Non-target members stay
	 *  idle for this order; the broker's completion predicate waits on the
	 *  effective subset only. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> TargetMembers;

	/** True if this order was injected internally by framework machinery
	 *  (e.g., AutoMoveThen prefix). Designer dispatches leave this false. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	bool bIsInternalPrefix = false;
};

/**
 * Input handed to USeinCommandBrokerResolver::ResolveDispatch. Mirrors the
 * live queued-order shape plus the broker's owning player ID and current anchor.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerOrderInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTagContainer Context;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector FormationEnd;

	/** The effective member set this order dispatches against — TargetMembers
	 *  from the queued order if non-empty, else the broker's full Members list.
	 *  Resolvers should iterate EffectiveMembers (not the broker's Members)
	 *  when building their dispatch plan. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinEntityHandle> EffectiveMembers;
};

/**
 * Typed payload for FSeinCommand when CommandType == Command.Type.BrokerOrder.
 * Carries the raw click context (smart-resolved sim-side by the broker resolver)
 * and formation endpoint for drag orders. Lives in FSeinCommand::Payload —
 * keeps the base command struct lean while allowing broker-order-specific
 * fields to evolve without touching unrelated command types.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerOrderPayload
{
	GENERATED_BODY()

	/** Click context tag container — RightClick + Target.Ground / Friendly /
	 *  Enemy / Neutral plus any designer-added target tags. Resolver interprets
	 *  per-member to pick which ability to activate. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FGameplayTagContainer CommandContext;

	/** Drag-order formation endpoint in world space. Zero = not a drag order. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Command")
	FFixedVector FormationEnd;
};

/**
 * One (member, ability, target) tuple produced by the resolver. The broker
 * system issues the per-member ActivateAbility internally on tick.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerMemberDispatch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle Member;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;
};

/**
 * Full resolver output for one order. The broker system walks
 * MemberDispatches on the dispatch tick and fires an internal ActivateAbility
 * against each member's FSeinAbilityData.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinBrokerDispatchPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	TArray<FSeinBrokerMemberDispatch> MemberDispatches;
};
