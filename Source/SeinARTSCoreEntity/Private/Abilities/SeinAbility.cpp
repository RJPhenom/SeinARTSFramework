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
#include "Components/SeinTagData.h"

void USeinAbility::InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem)
{
	OwnerEntity = Owner;
	WorldSubsystem = Subsystem;
	CooldownRemaining = FFixedPoint::Zero;
	bIsActive = false;
	AppliedOwnedTags.Reset();
}

void USeinAbility::ActivateAbility(FSeinEntityHandle Target, FFixedVector Location)
{
	TargetEntity = Target;
	TargetLocation = Location;
	bIsActive = true;
	CooldownRemaining = Cooldown;

	// Apply OwnedTags to the entity's runtime tag set, but only the ones not
	// already present — we record the diff so deactivate strips only what we
	// actually added. Anything already granted by the archetype or another
	// source is left untouched.
	AppliedOwnedTags.Reset();
	if (WorldSubsystem && !OwnedTags.IsEmpty())
	{
		if (FSeinTagData* TagComp = WorldSubsystem->GetComponent<FSeinTagData>(OwnerEntity))
		{
			for (const FGameplayTag& Tag : OwnedTags)
			{
				if (Tag.IsValid() && !TagComp->CombinedTags.HasTag(Tag))
				{
					AppliedOwnedTags.AddTag(Tag);
					TagComp->GrantedTags.AddTag(Tag);
				}
			}
			if (!AppliedOwnedTags.IsEmpty())
			{
				TagComp->RebuildCombinedTags();
			}
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

	// Strip only the OwnedTags we actually granted on activate — preserves
	// tags that were already present (archetype-granted, other ability, etc.).
	if (WorldSubsystem && !AppliedOwnedTags.IsEmpty())
	{
		if (FSeinTagData* TagComp = WorldSubsystem->GetComponent<FSeinTagData>(OwnerEntity))
		{
			for (const FGameplayTag& Tag : AppliedOwnedTags)
			{
				TagComp->GrantedTags.RemoveTag(Tag);
			}
			TagComp->RebuildCombinedTags();
		}
		AppliedOwnedTags.Reset();
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
