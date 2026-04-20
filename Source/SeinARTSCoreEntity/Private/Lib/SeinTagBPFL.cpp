/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagBPFL.cpp
 * @brief   Implementation of gameplay tag Blueprint nodes. All mutations
 *          route through USeinWorldSubsystem so refcounts and the global
 *          EntityTagIndex stay consistent.
 */

#include "Lib/SeinTagBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinTagData.h"

USeinWorldSubsystem* USeinTagBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinTagBPFL::SeinHasTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(EntityHandle);
	return TagComp ? TagComp->HasTag(Tag) : false;
}

bool USeinTagBPFL::SeinHasAnyTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(EntityHandle);
	return TagComp ? TagComp->HasAnyTag(Tags) : false;
}

bool USeinTagBPFL::SeinHasAllTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(EntityHandle);
	return TagComp ? TagComp->HasAllTags(Tags) : false;
}

bool USeinTagBPFL::SeinAddBaseTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->AddBaseTag(EntityHandle, Tag) : false;
}

bool USeinTagBPFL::SeinRemoveBaseTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->RemoveBaseTag(EntityHandle, Tag) : false;
}

void USeinTagBPFL::SeinReplaceBaseTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer NewBaseTags)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		Subsystem->ReplaceBaseTags(EntityHandle, NewBaseTags);
	}
}

void USeinTagBPFL::SeinGrantTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		Subsystem->GrantTag(EntityHandle, Tag);
	}
}

void USeinTagBPFL::SeinUngrantTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		Subsystem->UngrantTag(EntityHandle, Tag);
	}
}
