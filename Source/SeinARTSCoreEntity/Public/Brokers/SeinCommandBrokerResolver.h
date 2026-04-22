/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerResolver.h
 * @brief   Abstract Blueprintable UObject that decides, for a broker's member
 *          set + order context, which members run which abilities against
 *          which targets (DESIGN §5).
 *
 *          Designers subclass this for custom formation/dispatch behavior.
 *          Framework ships USeinDefaultCommandBrokerResolver (C++) as the
 *          default; projects override via `DefaultBrokerResolverClass` on
 *          `USeinARTSCoreSettings`.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinCommandBrokerResolver.generated.h"

class USeinWorldSubsystem;

/**
 * Abstract designer-pluggable dispatch resolver for CommandBrokers.
 *
 * `ResolveDispatch` runs when a new order hits a broker. Given the broker
 * handle + order input, it returns a dispatch plan enumerating per-member
 * (ability, target) tuples the broker system will issue internally.
 *
 * `ResolvePositions` is a helper for tight-formation layout. Default impl
 * returns the anchor for every member; subclasses override for class-clustered
 * spacing, ranks, wedges, etc.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup = (SeinARTS),
	meta = (DisplayName = "Command Broker Resolver"))
class SEINARTSCOREENTITY_API USeinCommandBrokerResolver : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Resolve a broker order into per-member dispatches.
	 *
	 * Implementations read the broker's cached CapabilityMap + Members list
	 * (via `World->GetComponent<FSeinCommandBrokerData>(BrokerHandle)`) and
	 * decide which member runs which ability. Members that don't appear in
	 * the returned plan are silently skipped for this order.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Dispatch"))
	FSeinBrokerDispatchPlan ResolveDispatch(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order);
	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order);

	/**
	 * Compute desired world positions for the given members around an anchor.
	 * Returned array is index-aligned with `Members`. Default implementation
	 * returns `Anchor` for every member (no formation); subclasses override
	 * for tight-rank / class-cluster / wedge layouts.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Broker", meta = (DisplayName = "Resolve Positions"))
	TArray<FFixedVector> ResolvePositions(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing);
	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing);
};
