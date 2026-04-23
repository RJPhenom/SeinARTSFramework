/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDefaultCommandBrokerResolver.cpp
 * @brief   Framework-default CommandBroker resolver (DESIGN §5).
 *
 *          Dispatch rule, per effective member:
 *            1. Delegate to ResolveMemberAbility (virtual — reads the member's
 *               own DefaultCommands / FallbackAbilityTag by default, or
 *               whatever a subclass overrides it to do).
 *            2. If the returned ability tag is valid and the member owns that
 *               ability, dispatch it against the order's target entity/location.
 *            3. Otherwise (invalid tag OR member doesn't have the ability): if
 *               TagAlongAbility is set and the member owns it, dispatch it
 *               toward the formation slot. Non-combatants tagging along with
 *               an attack order, etc.
 *            4. Else silently skip — member stays idle for this order.
 *
 *          Positions: uniform-spaced square-ish grid centered on the anchor,
 *          facing-rotated. Good-enough MVP; designer resolvers replace for
 *          tight ranks, wedges, class-clustered formations.
 */

#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

FSeinBrokerDispatchPlan USeinDefaultCommandBrokerResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const FSeinBrokerOrderInput& Order)
{
	FSeinBrokerDispatchPlan Plan;
	if (!World) return Plan;

	const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	if (!Broker) return Plan;

	// Iterate the EFFECTIVE members — the subset this order targets (or the full
	// member list if the order is unrestricted). Formation positions are computed
	// over the effective set so subset orders layout around the target sensibly
	// (a 1-member "go repair that wall" order places that member at the target,
	// not at slot 3 of a 5-unit grid).
	const TArray<FSeinEntityHandle>& Effective = Order.EffectiveMembers;
	if (Effective.Num() == 0) return Plan;

	const TArray<FFixedVector> Positions = ResolvePositions(
		World, Effective, Order.TargetLocation, FFixedQuaternion::Identity);

	Plan.MemberDispatches.Reserve(Effective.Num());

	for (int32 i = 0; i < Effective.Num(); ++i)
	{
		const FSeinEntityHandle Member = Effective[i];
		const FFixedVector MemberGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;

		const FSeinAbilityData* AC = World->GetComponent<FSeinAbilityData>(Member);
		if (!AC) continue;

		// Layer 1: per-member tag resolution. Virtual, so subclass overrides
		// apply here. Default impl reads FSeinAbilityData::ResolveCommandContext.
		const FGameplayTag ResolvedTag = ResolveMemberAbility(World, Member, Order.Context);

		// Primary: dispatch the resolved tag if the member owns that ability.
		if (ResolvedTag.IsValid() && AC->HasAbilityWithTag(ResolvedTag))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = ResolvedTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = Order.TargetLocation;
			Plan.MemberDispatches.Add(MD);
			continue;
		}

		// Tag-along: resolver-level fallback for members whose own tables didn't
		// map this context. Dispatches against the formation slot (cohesion),
		// not the order target. Opt-in — empty TagAlongAbility disables.
		if (TagAlongAbility.IsValid() && AC->HasAbilityWithTag(TagAlongAbility))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = TagAlongAbility;
			MD.TargetLocation = MemberGoal;
			Plan.MemberDispatches.Add(MD);
			continue;
		}
		// else: no capable ability — silently skip. Member stays idle.
	}

	return Plan;
}

TArray<FFixedVector> USeinDefaultCommandBrokerResolver::ResolvePositions_Implementation(
	USeinWorldSubsystem* /*World*/,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector Anchor,
	FFixedQuaternion Facing)
{
	TArray<FFixedVector> Out;
	const int32 N = Members.Num();
	if (N == 0) return Out;
	Out.Reserve(N);

	// Uniform square-ish grid: compute side length = ceil(sqrt(N)), then iterate
	// row/column centered on anchor with InterUnitSpacing. Units = UE world cm.
	const FFixedPoint Spacing = InterUnitSpacing;
	int32 Side = 1;
	while (Side * Side < N) ++Side;

	const FFixedPoint HalfExtent = (FFixedPoint::FromInt(Side - 1) * Spacing) / FFixedPoint::Two;

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Col = i % Side;
		const int32 Row = i / Side;

		// Offset in formation-local space (X forward, Y right).
		const FFixedVector LocalOffset(
			FFixedPoint::FromInt(Row) * Spacing - HalfExtent,
			FFixedPoint::FromInt(Col) * Spacing - HalfExtent,
			FFixedPoint::Zero
		);

		// Rotate by Facing, translate by Anchor.
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		Out.Add(Anchor + WorldOffset);
	}

	return Out;
}
