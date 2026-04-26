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

	ActorBridge = CreateDefaultSubobject<USeinActorBridge>(TEXT("ActorBridge"));
	ArchetypeDefinition = CreateDefaultSubobject<USeinArchetypeDefinition>(TEXT("ArchetypeDefinition"));
}

void ASeinActor::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void ASeinActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Snapshot the actor's world location into a deterministic FFixedVector
	// at edit time. The conversion runs once in the editor process; the
	// result is serialized to the .umap. At runtime, all clients (PC, Mac
	// Apple Silicon, mobile, console) read the same int64 bits from disk,
	// no FromFloat conversion in the chain. Cross-arch lockstep safe.
	//
	// `bFinished` is false during a drag, true on release. Snapshotting on
	// every intermediate doesn't hurt (cheap), and ensures Ctrl+Z / undo
	// also lands a fresh snapshot.
	PlacedSimLocation = FFixedVector::FromVector(GetActorLocation());
	bSimLocationBaked = true;

	if (bFinished)
	{
		MarkPackageDirty();
	}
}
#endif

void ASeinActor::InitializeWithEntity(FSeinEntityHandle Handle)
{
	if (!ActorBridge)
	{
		UE_LOG(LogTemp, Error, TEXT("ASeinActor::InitializeWithEntity - ActorBridge is null!"));
		return;
	}

	ActorBridge->SetEntityHandle(Handle);

	// Fire Blueprint event
	ReceiveEntityInitialized();

	UE_LOG(LogTemp, Log, TEXT("ASeinActor initialized with entity %s"), *Handle.ToString());
}

FSeinEntityHandle ASeinActor::GetEntityHandle() const
{
	if (!ActorBridge)
	{
		return FSeinEntityHandle::Invalid();
	}

	return ActorBridge->GetEntityHandle();
}

bool ASeinActor::HasValidEntity() const
{
	if (!ActorBridge)
	{
		return false;
	}

	return ActorBridge->HasValidEntity();
}
