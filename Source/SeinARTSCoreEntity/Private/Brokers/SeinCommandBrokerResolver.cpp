/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerResolver.cpp
 * @brief   Default-on-nothing base implementation. Subclasses (BP or C++)
 *          override ResolveDispatch/ResolvePositions.
 */

#include "Brokers/SeinCommandBrokerResolver.h"
#include "Components/SeinAbilityData.h"
#include "Simulation/SeinWorldSubsystem.h"

FGameplayTag USeinCommandBrokerResolver::ResolveMemberAbility_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle Member,
	const FGameplayTagContainer& Context)
{
	if (!World) return FGameplayTag();
	const FSeinAbilityData* AC = World->GetComponent<FSeinAbilityData>(Member);
	if (!AC) return FGameplayTag();
	return AC->ResolveCommandContext(Context);
}

FSeinBrokerDispatchPlan USeinCommandBrokerResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* /*World*/,
	FSeinEntityHandle /*BrokerHandle*/,
	const FSeinBrokerOrderInput& /*Order*/)
{
	// Abstract — subclasses provide a dispatch plan. Returning empty is safe
	// (the system will simply cull the broker when the order completes with
	// zero pending member dispatches).
	return FSeinBrokerDispatchPlan{};
}

TArray<FFixedVector> USeinCommandBrokerResolver::ResolvePositions_Implementation(
	USeinWorldSubsystem* /*World*/,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector Anchor,
	FFixedQuaternion /*Facing*/)
{
	TArray<FFixedVector> Out;
	Out.Init(Anchor, Members.Num());
	return Out;
}
