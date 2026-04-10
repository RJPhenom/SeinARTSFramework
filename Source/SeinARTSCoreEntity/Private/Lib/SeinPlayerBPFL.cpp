/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerBPFL.cpp
 * @brief   Implementation of player resource and entity query Blueprint nodes.
 */

#include "Lib/SeinPlayerBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinEntityPool.h"

USeinWorldSubsystem* USeinPlayerBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FFixedPoint USeinPlayerBPFL::SeinGetPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) return FFixedPoint::Zero;

	return State->GetResource(ResourceName);
}

void USeinPlayerBPFL::SeinAddPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName, FFixedPoint Amount)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) return;

	TMap<FName, FFixedPoint> ResourceMap;
	ResourceMap.Add(ResourceName, Amount);
	State->AddResources(ResourceMap);
}

bool USeinPlayerBPFL::SeinDeductPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName, FFixedPoint Amount)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) return false;

	TMap<FName, FFixedPoint> CostMap;
	CostMap.Add(ResourceName, Amount);

	if (!State->CanAfford(CostMap)) return false;
	return State->DeductResources(CostMap);
}

bool USeinPlayerBPFL::SeinCanPlayerAfford(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TMap<FName, FFixedPoint>& Cost)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) return false;

	return State->CanAfford(Cost);
}

TArray<FSeinEntityHandle> USeinPlayerBPFL::SeinGetPlayerEntities(const UObject* WorldContextObject, FSeinPlayerID PlayerID)
{
	TArray<FSeinEntityHandle> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();
	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		if (Subsystem->GetEntityOwner(Handle) == PlayerID)
		{
			Result.Add(Handle);
		}
	});

	return Result;
}
