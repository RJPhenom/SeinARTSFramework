/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityValidation.cpp
 * @brief   Declarative target-validation implementation.
 */

#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinTagData.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/SeinWorldSubsystem.h"

ESeinAbilityTargetValidationResult FSeinAbilityValidation::ValidateTarget(
	const USeinAbility& Ability,
	FSeinEntityHandle Owner,
	FSeinEntityHandle Target,
	const FFixedVector& Location,
	USeinWorldSubsystem& World)
{
	// Range check — owner → target-entity position if present, else owner → Location.
	// Zero range means "unlimited," skip the check.
	if (Ability.MaxRange > FFixedPoint::Zero)
	{
		const FSeinEntity* OwnerEntity = World.GetEntity(Owner);
		if (OwnerEntity)
		{
			FFixedVector TargetPos = Location;
			if (Target.IsValid())
			{
				if (const FSeinEntity* TargetEntity = World.GetEntity(Target))
				{
					TargetPos = TargetEntity->Transform.GetLocation();
				}
			}

			const FFixedPoint DistSq = FFixedVector::DistSquared(OwnerEntity->Transform.GetLocation(), TargetPos);
			const FFixedPoint RangeSq = Ability.MaxRange * Ability.MaxRange;
			if (DistSq > RangeSq)
			{
				return ESeinAbilityTargetValidationResult::OutOfRange;
			}
		}
	}

	// ValidTargetTags query — only meaningful when a target entity is specified.
	if (Target.IsValid() && !Ability.ValidTargetTags.IsEmpty())
	{
		const FSeinTagData* TargetTags = World.GetComponent<FSeinTagData>(Target);
		const FGameplayTagContainer& TagsToQuery = TargetTags ? TargetTags->CombinedTags
			: FGameplayTagContainer::EmptyContainer;
		if (!Ability.ValidTargetTags.Matches(TagsToQuery))
		{
			return ESeinAbilityTargetValidationResult::InvalidTarget;
		}
	}

	// Line-of-sight: consult the cross-module USeinWorldSubsystem::LineOfSightResolver
	// delegate, bound by USeinFogOfWarSubsystem at OnWorldBeginPlay. If unbound
	// (tests, fog-less games), LOS check trivially passes. Target position stays
	// FFixedVector end-to-end — no lossy FVector round-trip.
	if (Ability.bRequiresLineOfSight && World.LineOfSightResolver.IsBound())
	{
		const FSeinEntity* OwnerEntity = World.GetEntity(Owner);
		if (OwnerEntity)
		{
			const FSeinPlayerID OwnerPlayer = World.GetEntityOwner(Owner);
			FFixedVector TargetWorld = Location;
			if (Target.IsValid())
			{
				if (const FSeinEntity* TargetEntity = World.GetEntity(Target))
				{
					TargetWorld = TargetEntity->Transform.GetLocation();
				}
			}
			if (!World.LineOfSightResolver.Execute(OwnerPlayer, TargetWorld))
			{
				return ESeinAbilityTargetValidationResult::NoLineOfSight;
			}
		}
	}

	return ESeinAbilityTargetValidationResult::Valid;
}
