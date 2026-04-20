/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityBPFL.cpp
 * @brief   Implementation of ability system Blueprint nodes.
 */

#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityData.h"
#include "Abilities/SeinAbility.h"

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

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(AbilityTag);
	if (!Ability || Ability->IsOnCooldown() || Ability->bIsActive) return;

	Ability->ActivateAbility(TargetEntity, TargetLocation);
}

void USeinAbilityBPFL::SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return;

	if (AbilityComp->ActiveAbility)
	{
		AbilityComp->ActiveAbility->CancelAbility();
	}
}

bool USeinAbilityBPFL::SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return false;

	// FindAbilityByTag is non-const, so we need to search manually
	for (USeinAbility* Ability : AbilityComp->AbilityInstances)
	{
		if (Ability && Ability->AbilityTag == AbilityTag)
		{
			return Ability->IsOnCooldown();
		}
	}
	return false;
}

FFixedPoint USeinAbilityBPFL::SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return FFixedPoint::Zero;

	for (USeinAbility* Ability : AbilityComp->AbilityInstances)
	{
		if (Ability && Ability->AbilityTag == AbilityTag)
		{
			return Ability->CooldownRemaining;
		}
	}
	return FFixedPoint::Zero;
}

bool USeinAbilityBPFL::SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityData* AbilityComp = Subsystem->GetComponent<FSeinAbilityData>(EntityHandle);
	if (!AbilityComp) return false;

	return AbilityComp->HasAbilityWithTag(AbilityTag);
}
