/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentBPFL.cpp
 */

#include "Lib/SeinComponentBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Actor/SeinActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinComponentBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

namespace
{
	/** Find the USeinActorComponent on the bridged actor whose Resolve() returns the
	 *  given struct type. Linear scan — AC count per actor is small. */
	USeinActorComponent* FindMatchingActorComponent(const UObject* WorldContextObject, FSeinEntityHandle Handle, UScriptStruct* StructType)
	{
		if (!WorldContextObject || !StructType) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World) return nullptr;
		USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
		if (!Bridge) return nullptr;
		ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
		if (!Actor) return nullptr;

		TArray<USeinActorComponent*> ACs;
		Actor->GetComponents<USeinActorComponent>(ACs);
		for (USeinActorComponent* AC : ACs)
		{
			if (!AC) continue;
			const FInstancedStruct Resolved = AC->Resolve();
			if (Resolved.IsValid() && Resolved.GetScriptStruct() == StructType)
			{
				return AC;
			}
		}
		return nullptr;
	}
}

bool USeinComponentBPFL::SeinGetComponentData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: no subsystem or null struct type"));
		return false;
	}
	const ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(StructType);
	if (!Storage)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: no storage registered for %s"), *StructType->GetName());
		return false;
	}
	const void* Raw = Storage->GetComponentRaw(EntityHandle);
	if (!Raw)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: entity %s has no %s"), *EntityHandle.ToString(), *StructType->GetName());
		return false;
	}
	OutData.InitializeAs(StructType, static_cast<const uint8*>(Raw));
	return true;
}

bool USeinComponentBPFL::SeinGetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData, USeinActorComponent*& OutActorComponent)
{
	OutActorComponent = nullptr;
	if (!SeinGetComponentData(WorldContextObject, EntityHandle, StructType, OutData))
	{
		return false;
	}
	OutActorComponent = FindMatchingActorComponent(WorldContextObject, EntityHandle, StructType);
	return true;
}

TArray<FInstancedStruct> USeinComponentBPFL::SeinGetComponents(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	TArray<FInstancedStruct> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	for (const TPair<UScriptStruct*, ISeinComponentStorage*>& Pair : Subsystem->GetAllComponentStorages())
	{
		if (!Pair.Key || !Pair.Value) continue;
		if (const void* Raw = Pair.Value->GetComponentRaw(EntityHandle))
		{
			FInstancedStruct Inst;
			Inst.InitializeAs(Pair.Key, static_cast<const uint8*>(Raw));
			Result.Add(MoveTemp(Inst));
		}
	}
	return Result;
}
