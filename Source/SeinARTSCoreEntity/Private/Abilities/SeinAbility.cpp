/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbility.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Base ability class implementation. Tracks activation/cancel
 *				lifecycle including cost refund + cooldown timing per
 *				DESIGN §7 Q2a / Q3c / Q4c. Cost-deduct happens on the caller
 *				side (ProcessCommands::ActivateAbility); this class consumes
 *				the deduct snapshot via RecordDeductedCost.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Abilities/SeinAbility.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Lib/SeinResourceBPFL.h"

void USeinAbility::InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem)
{
	OwnerEntity = Owner;
	WorldSubsystem = Subsystem;
	CooldownRemaining = FFixedPoint::Zero;
	bIsActive = false;
	bCooldownStarted = false;
	DeductedCost.Amounts.Empty();
}

UWorld* USeinAbility::GetWorld() const
{
	// Skip during CDO construction — UObject systems probe GetWorld() while
	// laying out the class default object, before any subsystem is bound. The
	// nullptr return matches the UObject default for CDOs.
	if (HasAnyFlags(RF_ClassDefaultObject)) { return nullptr; }
	return WorldSubsystem ? WorldSubsystem->GetWorld() : nullptr;
}

void USeinAbility::ActivateAbility(FSeinEntityHandle Target, FFixedVector Location)
{
	TargetEntity = Target;
	TargetLocation = Location;
	bIsActive = true;

	// Start cooldown if the ability's timing fires on activate. OnEnd-timed abilities
	// defer cooldown until DeactivateAbility.
	if (CooldownStartTiming == ESeinCooldownStartTiming::OnActivate)
	{
		CooldownRemaining = Cooldown;
		bCooldownStarted = true;
	}

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

	// Refund on cancel ─ drive DESIGN §7 Q2a / Q3c policy
	if (bCancelled && WorldSubsystem)
	{
		if (bRefundCostOnCancel && !DeductedCost.IsEmpty())
		{
			const FSeinPlayerID Owner = WorldSubsystem->GetEntityOwner(OwnerEntity);
			USeinResourceBPFL::SeinRefund(WorldSubsystem, Owner, DeductedCost);
		}
		if (bRefundCooldownOnCancel && bCooldownStarted)
		{
			CooldownRemaining = FFixedPoint::Zero;
		}
	}
	// Start cooldown on natural end when timing is OnEnd and not already running
	else if (!bCancelled && CooldownStartTiming == ESeinCooldownStartTiming::OnEnd && !bCooldownStarted)
	{
		CooldownRemaining = Cooldown;
		bCooldownStarted = true;
	}

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

	DeductedCost.Amounts.Empty();

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
