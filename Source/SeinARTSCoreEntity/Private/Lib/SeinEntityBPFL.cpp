/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityBPFL.cpp
 * @brief   Implementation of entity lifecycle and query Blueprint nodes.
 */

#include "Lib/SeinEntityBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinEntityBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

namespace
{
	USeinArchetypeDefinition* ResolveArchetype(USeinWorldSubsystem* Subsystem, FSeinEntityHandle Handle)
	{
		if (!Subsystem) return nullptr;
		TSubclassOf<ASeinActor> ActorClass = Subsystem->GetEntityActorClass(Handle);
		if (!ActorClass) return nullptr;
		const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
		return CDO ? CDO->ArchetypeDefinition : nullptr;
	}
}

USeinArchetypeDefinition* USeinEntityBPFL::SeinGetArchetypeDefinition(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	USeinArchetypeDefinition* Def = ResolveArchetype(Subsystem, EntityHandle);
	if (!Def)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetArchetypeDefinition: entity %s invalid or has no archetype"), *EntityHandle.ToString());
	}
	return Def;
}

TArray<USeinArchetypeDefinition*> USeinEntityBPFL::SeinGetArchetypeDefinitionMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<USeinArchetypeDefinition*> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		USeinArchetypeDefinition* Def = ResolveArchetype(Subsystem, Handle);
		if (!Def) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetArchetypeDefinition (batch): null for entity %s"), *Handle.ToString()); }
		Result.Add(Def);
	}
	return Result;
}

FSeinEntityHandle USeinEntityBPFL::SeinSpawnEntity(const UObject* WorldContextObject, TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !ActorClass) return FSeinEntityHandle::Invalid();
	return Subsystem->SpawnEntity(ActorClass, SpawnTransform, OwnerPlayerID);
}

void USeinEntityBPFL::SeinDestroyEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;
	Subsystem->DestroyEntity(EntityHandle);
}

FFixedTransform USeinEntityBPFL::SeinGetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedTransform();
	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	return Entity ? Entity->Transform : FFixedTransform();
}

void USeinEntityBPFL::SeinSetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FFixedTransform& Transform)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;
	FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (Entity)
	{
		Entity->Transform = Transform;
	}
}

FSeinPlayerID USeinEntityBPFL::SeinGetEntityOwner(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinPlayerID::Neutral();
	return Subsystem->GetEntityOwner(EntityHandle);
}

bool USeinEntityBPFL::SeinIsEntityAlive(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;
	return Subsystem->IsEntityAlive(EntityHandle);
}

bool USeinEntityBPFL::SeinIsHandleValid(FSeinEntityHandle EntityHandle)
{
	return EntityHandle.IsValid();
}

FSeinEntityHandle USeinEntityBPFL::SeinInvalidHandle()
{
	return FSeinEntityHandle::Invalid();
}
