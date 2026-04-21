/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStatsBPFL.cpp
 * @brief   Stats BPFL implementation.
 */

#include "Lib/SeinStatsBPFL.h"
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinStatsBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinStatsBPFL::SeinBumpStat(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag StatTag, FFixedPoint Amount)
{
	if (!StatTag.IsValid()) return;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinPlayerState* State = Subsystem->GetPlayerStateMutable(PlayerID);
	if (!State) return;

	FFixedPoint& Counter = State->StatCounters.FindOrAdd(StatTag);
	Counter = Counter + Amount;
}

FFixedPoint USeinStatsBPFL::SeinGetStat(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag StatTag)
{
	if (!StatTag.IsValid()) return FFixedPoint::Zero;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) return FFixedPoint::Zero;

	const FFixedPoint* Found = State->StatCounters.Find(StatTag);
	return Found ? *Found : FFixedPoint::Zero;
}

TMap<FGameplayTag, FFixedPoint> USeinStatsBPFL::SeinGetAllStats(const UObject* WorldContextObject, FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return {};

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	return State ? State->StatCounters : TMap<FGameplayTag, FFixedPoint>();
}
