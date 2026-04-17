/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.cpp
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of base actor class for simulation-backed entities.
 */

#include "Actor/SeinActor.h"
#include "Actor/SeinActorBridge.h"
#include "Data/SeinArchetypeDefinition.h"

ASeinActor::ASeinActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SeinActorBridge = CreateDefaultSubobject<USeinActorBridge>(TEXT("SeinActorBridge"));
	ArchetypeDefinition = CreateDefaultSubobject<USeinArchetypeDefinition>(TEXT("ArchetypeDefinition"));
}

void ASeinActor::BeginPlay()
{
	Super::BeginPlay();
}

void ASeinActor::InitializeWithEntity(FSeinEntityHandle Handle)
{
	if (!SeinActorBridge)
	{
		UE_LOG(LogTemp, Error, TEXT("ASeinActor::InitializeWithEntity - SeinActorBridge is null!"));
		return;
	}

	SeinActorBridge->SetEntityHandle(Handle);

	// Fire Blueprint event
	ReceiveEntityInitialized();

	UE_LOG(LogTemp, Log, TEXT("ASeinActor initialized with entity %s"), *Handle.ToString());
}

FSeinEntityHandle ASeinActor::GetEntityHandle() const
{
	if (!SeinActorBridge)
	{
		return FSeinEntityHandle::Invalid();
	}

	return SeinActorBridge->GetEntityHandle();
}

bool ASeinActor::HasValidEntity() const
{
	if (!SeinActorBridge)
	{
		return false;
	}

	return SeinActorBridge->HasValidEntity();
}
