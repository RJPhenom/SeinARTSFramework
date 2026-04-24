/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVisibilitySubsystem.cpp
 */

#include "SeinFogOfWarVisibilitySubsystem.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "Components/SeinVisionData.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Actor/SeinActor.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Entity.h"

#include "Engine/World.h"
#include "Stats/Stats.h"

void USeinFogOfWarVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void USeinFogOfWarVisibilitySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool USeinFogOfWarVisibilitySubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

TStatId USeinFogOfWarVisibilitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinFogOfWarVisibilitySubsystem, STATGROUP_Tickables);
}

void USeinFogOfWarVisibilitySubsystem::Tick(float DeltaTime)
{
	// Poll-interval gate. Default 0 = every frame; bumping skips frames
	// without costing a poll. Stamps themselves are always current at the
	// last VisionTickInterval — this just throttles the react-rate.
	TimeSinceLastPoll += DeltaTime;
	if (PollInterval > 0.f && TimeSinceLastPoll < PollInterval) return;
	TimeSinceLastPoll = 0.f;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	USeinFogOfWarSubsystem* FogSub = World->GetSubsystem<USeinFogOfWarSubsystem>();
	if (!FogSub) return;
	USeinFogOfWar* Fog = FogSub->GetFogOfWar();
	if (!Fog) return;

	// Permissive when fog has no runtime data — matches LOS-resolver
	// behavior. Nothing hides; system is effectively a no-op.
	if (!Fog->HasRuntimeData()) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Sim || !Bridge) return;

	const FSeinPlayerID Observer = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(World);

	// Cache the FSeinVisionData storage once; we touch it per-entity for
	// emission mask lookups.
	const ISeinComponentStorage* VisionStorage = Sim->GetComponentStorageRaw(FSeinVisionData::StaticStruct());

	// Walk live entities. Bridge lookup is O(log N) per entity; per-frame
	// cost is O(N) over live entities. 1000 entities × 60 FPS = 60k
	// lookups/sec — comfortably under a millisecond on modern hardware.
	const bool bDisableColl = bDisableCollisionWhenHidden;
	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Bridge, Fog, VisionStorage, Observer, bDisableColl](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
			if (!Actor) return; // abstract entity (broker/squad) or not yet bridged

			// Owner's own units always visible regardless of fog. Covers
			// the common case without a bitfield lookup and dodges any
			// edge case where a unit hasn't stamped yet on tick 0.
			const FSeinPlayerID OwnerPlayer = Sim->GetEntityOwner(Handle);
			bool bVisible = (Observer.IsValid() && OwnerPlayer == Observer);
			if (!bVisible)
			{
				// Target's emission mask — which layer bits need to be
				// stamped in the observer's view for this entity to be
				// seen. Units without FSeinVisionData default to Normal
				// emission (props, destructibles etc. behave sensibly
				// without explicit vision config).
				uint8 EmissionMask = SEIN_FOW_BIT_NORMAL;
				if (VisionStorage)
				{
					if (const void* Raw = VisionStorage->GetComponentRaw(Handle))
					{
						EmissionMask = static_cast<const FSeinVisionData*>(Raw)->EmissionLayerMask;
					}
				}
				if (EmissionMask == 0)
				{
					bVisible = false; // entity configured as never-visible
				}
				else
				{
					const uint8 ObserverBits = Fog->GetCellBitfield(Observer, Entity.Transform.GetLocation());
					bVisible = (ObserverBits & EmissionMask) != 0;
				}
			}

			const bool bShouldBeHidden = !bVisible;
			if (Actor->IsHidden() != bShouldBeHidden)
			{
				Actor->SetActorHiddenInGame(bShouldBeHidden);
				if (bDisableColl)
				{
					Actor->SetActorEnableCollision(bVisible);
				}
			}
		});
}
