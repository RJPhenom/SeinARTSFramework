/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityBPFL.cpp
 * @brief   Implementation of ability system Blueprint nodes.
 */

#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinTagData.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Lib/SeinResourceBPFL.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinAbilityBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinAbilityBPFL::SeinGetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinAbilityData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData: no SeinWorldSubsystem in this world context"));
		return false;
	}
	const FSeinAbilityData* Data = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData: entity %s invalid or has no FSeinAbilityData"), *EntityHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

TArray<FSeinAbilityData> USeinAbilityBPFL::SeinGetAbilityDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinAbilityData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinAbilityData* Data = Subsystem->GetComponent<FSeinAbilityData>(Handle))
		{
			Result.Add(*Data);
		}
		else
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData (batch): skipping entity %s (invalid or no FSeinAbilityData)"), *Handle.ToString());
		}
	}
	return Result;
}

void USeinAbilityBPFL::SeinActivateAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return;

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag);
	if (!Ability || Ability->IsOnCooldown() || Ability->bIsActive) return;

	Ability->ActivateAbility(TargetEntity, TargetLocation);
}

void USeinAbilityBPFL::SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return;

	if (USeinAbility* Active = AbilityComp->GetActiveAbility(*Subsystem))
	{
		Active->CancelAbility();
	}
}

bool USeinAbilityBPFL::SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return false;

	if (const USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag))
	{
		return Ability->IsOnCooldown();
	}
	return false;
}

FFixedPoint USeinAbilityBPFL::SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return FFixedPoint::Zero;

	if (const USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag))
	{
		return Ability->CooldownRemaining;
	}
	return FFixedPoint::Zero;
}

bool USeinAbilityBPFL::SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return false;

	return AbilityComp->HasAbilityWithTag(*Subsystem, AbilityTag);
}

FSeinAbilityAvailability USeinAbilityBPFL::SeinGetAbilityAvailability(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag,
	FSeinEntityHandle OptionalTargetEntity,
	FFixedVector OptionalTargetLocation)
{
	FSeinAbilityAvailability Out;
	Out.AbilityTag = AbilityTag;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag);
	if (!Ability) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	Out.CooldownRemaining = Ability->CooldownRemaining;
	Out.bIsActive = Ability->bIsActive;

	const FSeinPlayerID Owner = Subsystem->GetEntityOwner(EntityHandle);
	Out.bCanAfford = USeinResourceBPFL::SeinCanAfford(WorldContextObject, Owner, Ability->ResourceCost);

	// Walk the same gate order as ProcessCommands::ActivateAbility and report
	// the first failing gate.
	if (Ability->IsOnCooldown())
	{
		Out.Reason = ESeinAbilityUnavailableReason::OnCooldown;
		return Out;
	}

	if (!Ability->ActivationBlockedTags.IsEmpty())
	{
		const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(EntityHandle);
		if (TagComp && TagComp->CombinedTags.HasAny(Ability->ActivationBlockedTags))
		{
			Out.Reason = ESeinAbilityUnavailableReason::BlockedByTag;
			return Out;
		}
	}

	const ESeinAbilityTargetValidationResult Validation = FSeinAbilityValidation::ValidateTarget(
		*Ability, EntityHandle, OptionalTargetEntity, OptionalTargetLocation, *Subsystem);
	switch (Validation)
	{
		case ESeinAbilityTargetValidationResult::OutOfRange:
			Out.Reason = ESeinAbilityUnavailableReason::OutOfRange; return Out;
		case ESeinAbilityTargetValidationResult::InvalidTarget:
			Out.Reason = ESeinAbilityUnavailableReason::InvalidTarget; return Out;
		case ESeinAbilityTargetValidationResult::NoLineOfSight:
			Out.Reason = ESeinAbilityUnavailableReason::NoLineOfSight; return Out;
		default: break;
	}

	if (!Ability->CanActivate())
	{
		Out.Reason = ESeinAbilityUnavailableReason::CanActivateFailed;
		return Out;
	}

	if (!Out.bCanAfford)
	{
		Out.Reason = ESeinAbilityUnavailableReason::Unaffordable;
		return Out;
	}

	Out.bAvailable = true;
	Out.Reason = ESeinAbilityUnavailableReason::None;
	return Out;
}
