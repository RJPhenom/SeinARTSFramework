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

	/** Ability tag the resolver interprets as "what kind of order is this"
	 *  (SeinARTS.Ability.Move / Attack / Harvest / …). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FGameplayTag ContextTag;

	/** Optional target entity (attack target, repair target, etc.). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	/** Target world location (move destination, attack point, rally, …). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	/** Optional second endpoint for drag orders (formation line end). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector FormationEnd;

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
	FGameplayTag ContextTag;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
	FFixedVector TargetLocation;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Broker")
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
