/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbility.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Base ability class implementation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Abilities/SeinAbility.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Abilities/SeinLatentActionManager.h"

void USeinAbility::InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem)
{
	OwnerEntity = Owner;
	WorldSubsystem = Subsystem;
	CooldownRemaining = FFixedPoint::Zero;
	bIsActive = false;
}

void USeinAbility::ActivateAbility(FSeinEntityHandle Target, FFixedVector Location)
{
	TargetEntity = Target;
	TargetLocation = Location;
	bIsActive = true;
	CooldownRemaining = Cooldown;

	// Grant each OwnedTag. Refcounting (DESIGN.md §2) means overlapping grants
	// from BaseTags, other abilities, or effects stay present — DeactivateAbility
	// just releases our refcount.
	if (WorldSubsystem)
	{
		for (const FGameplayTag& Tag : OwnedTags)
		{
			WorldSubsystem->GrantTag(OwnerEntity, Tag);
		}
	}

	OnActivate();
}

void USeinAbility::TickAbility(FFixedPoint DeltaTime)
{
	if (bIsActive)
	{
		OnTick(DeltaTime);
	}
}

void USeinAbility::DeactivateAbility(bool bCancelled)
{
	if (!bIsActive)
	{
		return;
	}

	bIsActive = false;

	// Tear down any latent actions belonging to this ability. Without this,
	// actions like SeinMoveTo linger after EndAbility/CancelAbility and keep
	// ticking against a logically-inactive ability.
	if (WorldSubsystem && WorldSubsystem->LatentActionManager)
	{
		WorldSubsystem->LatentActionManager->CancelActionsForAbility(this);
	}

	if (WorldSubsystem)
	{
		for (const FGameplayTag& Tag : OwnedTags)
		{
			WorldSubsystem->UngrantTag(OwnerEntity, Tag);
		}
	}

	OnEnd(bCancelled);
}

void USeinAbility::EndAbility()
{
	DeactivateAbility(false);
}

void USeinAbility::CancelAbility()
{
	DeactivateAbility(true);
}

void USeinAbility::TickCooldown(FFixedPoint DeltaTime)
{
	if (CooldownRemaining > FFixedPoint::Zero)
	{
		CooldownRemaining = CooldownRemaining - DeltaTime;
		if (CooldownRemaining < FFixedPoint::Zero)
		{
			CooldownRemaining = FFixedPoint::Zero;
		}
	}
}

bool USeinAbility::IsOnCooldown() const
{
	return CooldownRemaining > FFixedPoint::Zero;
}
