/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapSubsystem.cpp
 */

#include "GameMode/SeinMatchBootstrapSubsystem.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinWorldSettings.h"
#include "GameMode/SeinGameMode.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinMatchSettings.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Types/Transform.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void USeinMatchBootstrapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the actor bridge to initialize first, then disable its
	// OnWorldBeginPlay auto-register. We're going to drive bootstrap order
	// explicitly so server and clients produce identical entity-ID sequences:
	//
	//   server:  GameMode pre-spawns slot entities (InitGame, before BeginPlay)
	//            → THEN our OnWorldBeginPlay calls bridge.RegisterAllPlacedActors
	//   client:  no GameMode → our OnWorldBeginPlay pre-spawns slot entities
	//            → THEN calls bridge.RegisterAllPlacedActors
	//
	// Either way, slot entities go in first (IDs 1..N), then placed actors
	// (sorted by name for further determinism) get IDs N+1..M. Lockstep stays
	// bit-identical from tick zero.
	Collection.InitializeDependency(USeinActorBridgeSubsystem::StaticClass());

	UWorld* World = GetWorld();
	if (USeinActorBridgeSubsystem* Bridge = World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr)
	{
		Bridge->SetAutoRegisterOnBeginPlay(false);
	}
}

void USeinMatchBootstrapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* Sub = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinMatchBootstrap: USeinWorldSubsystem missing."));
		return;
	}

	// On the server (incl. ListenServer + DedicatedServer) GameMode already
	// pre-spawned slot entities in InitGame. On clients (NetMode == NM_Client)
	// we mirror that work in the SAME order so entity IDs match.
	const ENetMode NetMode = InWorld.GetNetMode();
	if (NetMode == NM_Client)
	{
		if (ASeinWorldSettings* SeinWS = Cast<ASeinWorldSettings>(InWorld.GetWorldSettings()))
		{
			RunSlotPreRegistration(InWorld, *Sub, SeinWS->GetEffectiveMatchSettings());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap: no ASeinWorldSettings on this level — skipping client pre-reg (matches GameMode legacy path)."));
		}
	}

	// Whether we just pre-spawned slot entities (client) or GameMode did it
	// earlier (server), placed actors get registered LAST so their entity IDs
	// always come after slot starts. Stable-sort by name (inside the bridge)
	// + skip-already-linked makes this deterministic across machines.
	if (USeinActorBridgeSubsystem* Bridge = InWorld.GetSubsystem<USeinActorBridgeSubsystem>())
	{
		Bridge->RegisterAllPlacedActors(InWorld);
	}
}

void USeinMatchBootstrapSubsystem::RunSlotPreRegistration(UWorld& InWorld, USeinWorldSubsystem& Sub, const FSeinMatchSettings& Settings)
{
	// Resolve the GameMode CDO once — used both for DefaultFactionID fallback
	// (must match server's value, NOT a hardcoded `FSeinFactionID(1)`) and for
	// the StartingResources copy below. Server's RegisterPlayerWithSim reads
	// these from `this` (a live GameMode); we read from the CDO since clients
	// have no GameMode instance.
	const ASeinGameMode* GMCDO = nullptr;
	{
		UClass* GMClass = nullptr;
		if (AWorldSettings* WS = InWorld.GetWorldSettings())
		{
			GMClass = WS->DefaultGameMode;
		}
		if (GMClass && GMClass->IsChildOf(ASeinGameMode::StaticClass()))
		{
			GMCDO = GMClass->GetDefaultObject<ASeinGameMode>();
		}
	}
	const FSeinFactionID DefaultFaction = GMCDO ? GMCDO->DefaultFactionID : FSeinFactionID(1);

	// SAME LOOP as SeinGameMode::PreRegisterMatchSlots. Iteration order over
	// Settings.Slots is deterministic (it's a TArray), and the PlayerStart
	// lookup is by SlotIndex match (not iteration order), so resulting
	// entity IDs are identical across server and every client.
	int32 NumSpawned = 0;
	for (const FSeinMatchSlot& Slot : Settings.Slots)
	{
		if (Slot.State != ESeinSlotState::Human && Slot.State != ESeinSlotState::AI)
		{
			continue;
		}
		if (Slot.SlotIndex <= 0)
		{
			continue;
		}

		const FSeinPlayerID SlotPlayerID(static_cast<uint8>(Slot.SlotIndex));
		// Use the actual GameMode CDO's DefaultFactionID (resolved above).
		// Hardcoding FSeinFactionID(1) here would diverge from server whenever
		// the project sets DefaultFactionID to anything else — silent state
		// divergence at registration time.
		const FSeinFactionID Faction = Slot.FactionID.IsValid() ? Slot.FactionID : DefaultFaction;

		Sub.RegisterPlayer(SlotPlayerID, Faction, Slot.TeamID);

		// Mirror the server's `RegisterPlayerWithSim` — copy StartingResources
		// from the GameMode CDO onto the just-registered player state.
		// Without this, server PlayerState has resources but client PlayerState
		// has none → state-hash mismatch from tick 1.
		if (GMCDO)
		{
			if (FSeinPlayerState* State = Sub.GetPlayerState(SlotPlayerID))
			{
				for (const TPair<FGameplayTag, FFixedPoint>& Pair : GMCDO->StartingResources)
				{
					State->SetResource(Pair.Key, Pair.Value);
				}
			}
		}

		// Find the SeinPlayerStart that targets this slot.
		ASeinPlayerStart* Start = nullptr;
		for (TActorIterator<ASeinPlayerStart> It(&InWorld); It; ++It)
		{
			if (It->PlayerSlot == Slot.SlotIndex)
			{
				Start = *It;
				break;
			}
		}

		if (!Start || !Start->SpawnEntity)
		{
			continue;
		}

		// Read the editor-baked snapshot for cross-arch determinism (matches
		// SeinGameMode::SpawnStartEntity exactly). Re-saving the level bakes
		// PlacedSimLocation so this branch is preferred; runtime FromFloat
		// is a fallback that loses determinism guarantees.
		FFixedTransform SpawnTransform;
		if (Start->bSimLocationBaked)
		{
			SpawnTransform.Location = Start->PlacedSimLocation;
		}
		else
		{
			const FVector Loc = Start->GetActorLocation();
			UE_LOG(LogTemp, Warning,
				TEXT("SeinMatchBootstrap: PlayerStart %s has stale PlacedSimLocation — falling back to runtime FromFloat at %s. NOT cross-arch deterministic until the level is re-saved."),
				*Start->GetName(), *Loc.ToString());
			SpawnTransform.Location = FFixedVector(
				FFixedPoint::FromFloat(Loc.X),
				FFixedPoint::FromFloat(Loc.Y),
				FFixedPoint::FromFloat(Loc.Z));
		}

		const FSeinEntityHandle Handle = Sub.SpawnEntity(Start->SpawnEntity, SpawnTransform, SlotPlayerID);
		if (Handle.IsValid())
		{
			++NumSpawned;
			UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap [Client]: spawned %s for %s"),
				*Start->SpawnEntity->GetName(), *SlotPlayerID.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SeinMatchBootstrap [Client]: failed to spawn start entity %s for %s"),
				*Start->SpawnEntity->GetName(), *SlotPlayerID.ToString());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SeinMatchBootstrap [Client]: pre-registered slots from match settings, spawned %d start entity(ies). Should match server's pre-reg log."),
		NumSpawned);
}
