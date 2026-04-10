/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTagBPFL.cpp
 * @brief   Implementation of gameplay tag Blueprint nodes.
 */

#include "Lib/SeinTagBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinTagComponent.h"

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

	const FSeinTagComponent* TagComp = Subsystem->GetComponent<FSeinTagComponent>(EntityHandle);
	return TagComp ? TagComp->HasTag(Tag) : false;
}

bool USeinTagBPFL::SeinHasAnyTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinTagComponent* TagComp = Subsystem->GetComponent<FSeinTagComponent>(EntityHandle);
	return TagComp ? TagComp->HasAnyTag(Tags) : false;
}

bool USeinTagBPFL::SeinHasAllTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinTagComponent* TagComp = Subsystem->GetComponent<FSeinTagComponent>(EntityHandle);
	return TagComp ? TagComp->HasAllTags(Tags) : false;
}

void USeinTagBPFL::SeinAddTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinTagComponent* TagComp = Subsystem->GetComponent<FSeinTagComponent>(EntityHandle);
	if (TagComp)
	{
		TagComp->BaseTags.AddTag(Tag);
		TagComp->RebuildCombinedTags();
	}
}

void USeinTagBPFL::SeinRemoveTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinTagComponent* TagComp = Subsystem->GetComponent<FSeinTagComponent>(EntityHandle);
	if (TagComp)
	{
		TagComp->BaseTags.RemoveTag(Tag);
		TagComp->RebuildCombinedTags();
	}
}
