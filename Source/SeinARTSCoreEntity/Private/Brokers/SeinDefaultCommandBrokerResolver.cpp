/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDefaultCommandBrokerResolver.cpp
 * @brief   Framework-default CommandBroker resolver (DESIGN §5).
 *
 *          Dispatch rule:
 *            - If ContextTag is valid and the member has an ability with that
 *              tag, dispatch it against the order's target.
 *            - Otherwise fall back to SeinARTS.Ability.Move toward the
 *              member's formation-assigned world position (non-combatants
 *              tagging along with an attack order, etc.).
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
	if (!Broker || Broker->Members.Num() == 0) return Plan;

	// Per-member positions around the order anchor (target location).
	const TArray<FFixedVector> Positions = ResolvePositions_Implementation(
		World, Broker->Members, Order.TargetLocation, FFixedQuaternion::Identity);

	Plan.MemberDispatches.Reserve(Broker->Members.Num());

	for (int32 i = 0; i < Broker->Members.Num(); ++i)
	{
		const FSeinEntityHandle Member = Broker->Members[i];
		const FFixedVector MemberGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;

		const FSeinAbilityData* AC = World->GetComponent<FSeinAbilityData>(Member);
		if (!AC) continue;

		// Primary: dispatch ContextTag if available on this member.
		const bool bHasContextAbility = Order.ContextTag.IsValid() && AC->HasAbilityWithTag(Order.ContextTag);
		if (bHasContextAbility)
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = Order.ContextTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = Order.TargetLocation;
			Plan.MemberDispatches.Add(MD);
			continue;
		}

		// Fallback: move toward the formation-assigned goal if this member
		// has a Move ability. Non-combatant tag-along keeps formations cohesive.
		if (AC->HasAbilityWithTag(SeinARTSTags::Ability_Move))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = SeinARTSTags::Ability_Move;
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
	// row/column centered on anchor with fixed inter-unit spacing. Units = world
	// units (cm under UE's convention); 150cm is a reasonable infantry spread.
	const FFixedPoint Spacing = FFixedPoint::FromInt(150);
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
