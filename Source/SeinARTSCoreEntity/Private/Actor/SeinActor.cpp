/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.cpp
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of base actor class for simulation-backed entities.
 */

#include "Actor/SeinActor.h"
#include "Actor/SeinActorComponent.h"
#include "Data/SeinArchetypeDefinition.h"

ASeinActor::ASeinActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SeinComponent = CreateDefaultSubobject<USeinActorComponent>(TEXT("SeinComponent"));
	ArchetypeDefinition = CreateDefaultSubobject<USeinArchetypeDefinition>(TEXT("ArchetypeDefinition"));
}

void ASeinActor::BeginPlay()
{
	Super::BeginPlay();
}

void ASeinActor::InitializeWithEntity(FSeinEntityHandle Handle)
{
	if (!SeinComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("ASeinActor::InitializeWithEntity - SeinComponent is null!"));
		return;
	}

	SeinComponent->SetEntityHandle(Handle);

	// Fire Blueprint event
	ReceiveEntityInitialized();

	UE_LOG(LogTemp, Log, TEXT("ASeinActor initialized with entity %s"), *Handle.ToString());
}

FSeinEntityHandle ASeinActor::GetEntityHandle() const
{
	if (!SeinComponent)
	{
		return FSeinEntityHandle::Invalid();
	}

	return SeinComponent->GetEntityHandle();
}

bool ASeinActor::HasValidEntity() const
{
	if (!SeinComponent)
	{
		return false;
	}

	return SeinComponent->HasValidEntity();
}
