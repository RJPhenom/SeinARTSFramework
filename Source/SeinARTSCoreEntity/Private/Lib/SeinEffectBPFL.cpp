/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectBPFL.cpp
 * @brief   Implementation of effect system Blueprint nodes.
 */

#include "Lib/SeinEffectBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinActiveEffectsData.h"
#include "Effects/SeinActiveEffect.h"
#include "Events/SeinVisualEvent.h"

USeinWorldSubsystem* USeinEffectBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

int32 USeinEffectBPFL::SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, const FSeinEffectDefinition& EffectDefinition, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	FSeinActiveEffectsData* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsData>(TargetHandle);
	if (!EffectsComp) return 0;

	FSeinActiveEffect NewEffect;
	NewEffect.Definition = EffectDefinition;
	NewEffect.Source = SourceHandle;
	NewEffect.Target = TargetHandle;
	NewEffect.RemainingDuration = EffectDefinition.Duration;
	NewEffect.CurrentStacks = 1;
	NewEffect.TimeSinceLastPeriodic = FFixedPoint::Zero;

	uint32 InstanceID = EffectsComp->AddEffect(NewEffect);

	Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(TargetHandle, EffectDefinition.EffectTag, true));

	return static_cast<int32>(InstanceID);
}

void USeinEffectBPFL::SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int32 EffectInstanceID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinActiveEffectsData* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsData>(TargetHandle);
	if (!EffectsComp) return;

	EffectsComp->RemoveEffect(static_cast<uint32>(EffectInstanceID));
}

void USeinEffectBPFL::SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinActiveEffectsData* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsData>(TargetHandle);
	if (!EffectsComp) return;

	EffectsComp->RemoveEffectsWithTag(Tag);
}

bool USeinEffectBPFL::SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinActiveEffectsData* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsData>(TargetHandle);
	if (!EffectsComp) return false;

	return EffectsComp->HasEffectWithTag(Tag);
}

int32 USeinEffectBPFL::SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FName EffectName)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	const FSeinActiveEffectsData* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsData>(TargetHandle);
	if (!EffectsComp) return 0;

	return EffectsComp->GetStackCount(EffectName);
}
