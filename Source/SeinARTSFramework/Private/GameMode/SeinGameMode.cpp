/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.cpp
 * @brief   RTS game mode implementation.
 */

#include "GameMode/SeinGameMode.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinWorldSettings.h"
#include "Player/SeinPlayerController.h"
#include "Player/SeinCameraPawn.h"
#include "HUD/SeinHUD.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinPlayerState.h"
#include "Actor/SeinActor.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "SeinNetSubsystem.h"
#include "SeinLobbySubsystem.h"

namespace
{
	// True when a networked lockstep session will start the sim (host's relay
	// arrives → NotifyLocalSlotAssigned → StartSimulation, with the gate
	// already bound). In that case GameMode auto-start MUST defer, otherwise
	// the sim runs ungated for several frames before the gate engages and the
	// initial heartbeats (turns 0..N) are silently skipped — gate then stalls
	// forever on a turn no one ever submitted for.
	static bool ShouldDeferAutoStartToNetwork(const UWorld* World)
	{
		if (!World) return false;
		const UGameInstance* GI = World->GetGameInstance();
		if (!GI) return false;
		const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		return Net && Net->IsNetworkingActive();
	}
}

ASeinGameMode::ASeinGameMode()
{
	DefaultPawnClass = ASeinCameraPawn::StaticClass();
	PlayerControllerClass = ASeinPlayerController::StaticClass();
	HUDClass = ASeinHUD::StaticClass();
}

void ASeinGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// Phase 3d: register factions from plugin settings BEFORE any RegisterPlayer
	// looks up ResourceKit data. Same call also fires in
	// USeinMatchBootstrapSubsystem::OnWorldBeginPlay on clients, against the
	// same settings array → bit-identical Factions map across all peers.
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sub = World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sub->RegisterFactionsFromSettings();
		}
	}

	// Pre-register match slots BEFORE the PIE login flow fires. In PIE, the
	// editor injects the local player via SpawnPlayActor → PostLogin →
	// HandleStartingNewPlayer *before* UWorld::BeginPlay runs — so doing this
	// in our BeginPlay is too late and the legacy auto-assign path runs first,
	// double-spawning slot 1's entity. InitGame fires while subsystems are
	// already initialized but before any controller logs in.
	if (const FSeinMatchSettings* Settings = ResolveMatchSettingsForWorld())
	{
		ResolvedMatchSettings = *Settings;
		bMatchSettingsResolved = true;
		PreRegisterMatchSlots();
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinGameMode: No SeinWorldSettings on this level — using legacy per-controller flow"));
	}
}

// ==================== Match-Settings Pre-Registration ====================

const FSeinMatchSettings* ASeinGameMode::ResolveMatchSettingsForWorld() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	// Phase 3b: GI-scoped lobby snapshot is now the highest-priority source.
	// USeinLobbySubsystem publishes via Sein.Net.StartMatch (and the Phase 3c
	// map-travel handoff). When a snapshot exists AND has at least one slot,
	// prefer it — it's the live lobby's intent, captured fresh.
	//
	// Pre-3c single-map flow note: on the FIRST map's InitGame, no players
	// have connected yet → no snapshot → this branch falls through to
	// WorldSettings (existing PIE behavior preserved). The snapshot becomes
	// meaningful only after a future map travel where the new map's InitGame
	// runs with the snapshot already published.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
		{
			if (Lobby->HasPublishedSnapshot() && Lobby->GetPublishedSnapshot().Slots.Num() > 0)
			{
				UE_LOG(LogTemp, Log,
					TEXT("SeinGameMode: ResolveMatchSettingsForWorld → using lobby snapshot (%d slot(s))."),
					Lobby->GetPublishedSnapshot().Slots.Num());
				return &Lobby->GetPublishedSnapshot();
			}
		}
	}

	// Fallback: per-level WorldSettings (the existing PIE / scenario-load path).
	if (ASeinWorldSettings* SeinWS = Cast<ASeinWorldSettings>(World->GetWorldSettings()))
	{
		return &SeinWS->GetEffectiveMatchSettings();
	}
	return nullptr;
}

void ASeinGameMode::PreRegisterMatchSlots()
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* Subsystem = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: USeinWorldSubsystem missing — cannot pre-register match slots"));
		return;
	}

	// Inventory the level's SeinPlayerStarts up front so the log shows
	// designers exactly what's available. Without this, level data bugs
	// (PlayerSlot=0 on a start meant for slot 2, duplicate starts, missing
	// starts) only surface as the silent ChoosePlayerStart fallback —
	// player ends up at the wrong spawn with no clue why.
	{
		TArray<FString> StartReport;
		for (TActorIterator<ASeinPlayerStart> It(World); It; ++It)
		{
			StartReport.Add(FString::Printf(TEXT("%s(PlayerSlot=%d)"), *It->GetName(), It->PlayerSlot));
		}
		UE_LOG(LogTemp, Log,
			TEXT("SeinGameMode: Level has %d SeinPlayerStart(s): [%s]"),
			StartReport.Num(), *FString::Join(StartReport, TEXT(", ")));
	}

	TSet<int32> SeenSlots;
	int32 MaxSlotIndex = 0;
	int32 NumSpawned = 0;

	for (const FSeinMatchSlot& Slot : ResolvedMatchSettings.Slots)
	{
		MaxSlotIndex = FMath::Max(MaxSlotIndex, Slot.SlotIndex);

		// Open + Closed don't pre-spawn. Open slots get filled when a controller
		// connects (legacy auto-assign fallback in HandleStartingNewPlayer).
		if (Slot.State != ESeinSlotState::Human && Slot.State != ESeinSlotState::AI)
		{
			continue;
		}

		if (Slot.SlotIndex <= 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SeinGameMode: Match slot has invalid SlotIndex %d — skipping"), Slot.SlotIndex);
			continue;
		}
		if (SeenSlots.Contains(Slot.SlotIndex))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SeinGameMode: Duplicate SlotIndex %d in match settings — keeping first"), Slot.SlotIndex);
			continue;
		}
		SeenSlots.Add(Slot.SlotIndex);

		const FSeinPlayerID SlotPlayerID(static_cast<uint8>(Slot.SlotIndex));
		const FSeinFactionID Faction = Slot.FactionID.IsValid() ? Slot.FactionID : DefaultFactionID;

		RegisterPlayerWithSim(SlotPlayerID, Faction, Slot.TeamID);

		// Find the SeinPlayerStart anchored to this slot. Walk all starts (no
		// claimed-status filter — claims happen later, when controllers bind).
		ASeinPlayerStart* Start = nullptr;
		for (TActorIterator<ASeinPlayerStart> It(World); It; ++It)
		{
			if (It->PlayerSlot == Slot.SlotIndex)
			{
				Start = *It;
				break;
			}
		}

		if (Start)
		{
			SpawnStartEntity(Start, SlotPlayerID);
			++NumSpawned;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SeinGameMode: Slot %d (%s) has no SeinPlayerStart with matching PlayerSlot — "
					 "player registered but no start entity spawned"),
				Slot.SlotIndex,
				Slot.State == ESeinSlotState::Human ? TEXT("Human") : TEXT("AI"));
		}
	}

	// Bump NextPlayerIDValue past the highest pre-registered slot so any later
	// auto-assigned controllers (Open slots / no-manifest fallback) don't
	// collide with reserved slot IDs.
	NextPlayerIDValue = FMath::Max(NextPlayerIDValue, static_cast<uint8>(MaxSlotIndex + 1));

	UE_LOG(LogTemp, Log,
		TEXT("SeinGameMode: Pre-registered %d slot(s) from match settings, spawned %d entity(ies). NextPlayerIDValue=%d"),
		SeenSlots.Num(), NumSpawned, NextPlayerIDValue);

	// Match is set up — start the sim now rather than waiting for the first
	// human controller. Lets AI-only / spectator-load scenarios run.
	// EXCEPT: when networked, defer to USeinNetSubsystem so the sim starts
	// AFTER the gate is bound (otherwise the first ungated frames mute the
	// initial heartbeats and the lockstep gate stalls forever).
	if (bAutoStartSimulation && !bSimulationStarted)
	{
		if (ShouldDeferAutoStartToNetwork(World))
		{
			UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Networked session — deferring sim auto-start to USeinNetSubsystem (slot-assignment hook)."));
		}
		else
		{
			Subsystem->StartSimulation();
			bSimulationStarted = true;
			UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Simulation auto-started after slot pre-registration"));
		}
	}
}

// ==================== Player Start Selection ====================

AActor* ASeinGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Match-manifest path: route the connecting controller to the next Human
	// slot whose SeinPlayerStart hasn't been bound yet. AI / Closed / Open slots
	// are NEVER returned here — AI slots are owned by registered AI controllers
	// (DESIGN §16), Closed is locked, Open gets handled via legacy fallback.
	if (bMatchSettingsResolved)
	{
		// Routable states for connecting PCs: Human (designer pre-marked) or
		// Open (unclaimed seat). AI is owned by registered AI controllers
		// (DESIGN §16). Closed is locked. Without Open in the routable set,
		// any slot the designer left as Open in WorldSettings becomes
		// unreachable — connecting PCs fall through to Super::ChoosePlayerStart
		// which picks an arbitrary PlayerStart and the player visually spawns
		// at the wrong location (the spawn-bug user reproduced).
		auto IsRoutableForConnectingPC = [](ESeinSlotState State)
		{
			return State == ESeinSlotState::Human || State == ESeinSlotState::Open;
		};

		for (const FSeinMatchSlot& Slot : ResolvedMatchSettings.Slots)
		{
			if (!IsRoutableForConnectingPC(Slot.State)) continue;
			if (ClaimedSlots.Contains(Slot.SlotIndex)) continue;

			ASeinPlayerStart* MatchedStart = nullptr;
			int32 PlayerStartCount = 0;
			for (TActorIterator<ASeinPlayerStart> It(GetWorld()); It; ++It)
			{
				++PlayerStartCount;
				if (It->PlayerSlot == Slot.SlotIndex)
				{
					MatchedStart = *It;
					break;
				}
			}
			if (MatchedStart)
			{
				UE_LOG(LogTemp, Log,
					TEXT("SeinGameMode: ChoosePlayerStart matched %s to slot %d (state=%d) → %s"),
					*GetNameSafe(Player), Slot.SlotIndex, (int32)Slot.State, *MatchedStart->GetName());
				return MatchedStart;
			}

			// SPAWN BUG diagnostic: slot is unclaimed AND there's no
			// SeinPlayerStart with matching PlayerSlot. Without a clear log
			// the controller silently falls through to Super, lands at a
			// random / wrong start, and the player visually spawns "at one
			// of the other spawns" (the user's complaint). List every
			// PlayerStart we DID find so the level data fix is obvious.
			TArray<FString> StartReport;
			for (TActorIterator<ASeinPlayerStart> It(GetWorld()); It; ++It)
			{
				StartReport.Add(FString::Printf(TEXT("%s(PlayerSlot=%d)"), *It->GetName(), It->PlayerSlot));
			}
			UE_LOG(LogTemp, Warning,
				TEXT("SeinGameMode: ChoosePlayerStart could not find a SeinPlayerStart with PlayerSlot=%d for slot %d. Level has %d SeinPlayerStart(s): [%s]. Falling through to next slot — controller will likely spawn at the wrong location (visual bug). Fix: set PlayerSlot=%d on the intended SeinPlayerStart in the level."),
				Slot.SlotIndex, Slot.SlotIndex, PlayerStartCount,
				*FString::Join(StartReport, TEXT(", ")), Slot.SlotIndex);
		}

		// Manifest exhausted (lobby filled / over capacity). Fall back to base
		// class so the controller still gets a default spawn somewhere.
		// Diagnostic: dump every slot's state + claim status so the cause is
		// obvious in the log. The user's most common bugs here are:
		//   1) Designer marked extra slots as AI/Closed in WorldSettings, but
		//      tried to connect more humans than there are Human/Open slots.
		//   2) ClaimedSlots already contains every routable slot (capacity).
		FString SlotsReport;
		for (const FSeinMatchSlot& Slot : ResolvedMatchSettings.Slots)
		{
			const TCHAR* StateStr = TEXT("?");
			switch (Slot.State)
			{
			case ESeinSlotState::Open:   StateStr = TEXT("Open"); break;
			case ESeinSlotState::Human:  StateStr = TEXT("Human"); break;
			case ESeinSlotState::AI:     StateStr = TEXT("AI"); break;
			case ESeinSlotState::Closed: StateStr = TEXT("Closed"); break;
			}
			SlotsReport += FString::Printf(TEXT("[%d:%s%s] "),
				Slot.SlotIndex, StateStr,
				ClaimedSlots.Contains(Slot.SlotIndex) ? TEXT("|claimed") : TEXT(""));
		}
		UE_LOG(LogTemp, Warning,
			TEXT("SeinGameMode: ChoosePlayerStart — manifest exhausted for %s. Slots: %s. Routable states are Human and Open; AI/Closed slots are not assigned to connecting PCs. Falling back to Super (random PlayerStart). To fix: set additional WorldSettings slots to Human or Open."),
			*GetNameSafe(Player), *SlotsReport);
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Legacy fallback path (no match-settings manifest configured).
	const int32 TargetSlot = static_cast<int32>(NextPlayerIDValue);

	if (const int32* PreAssigned = PreAssignedSlots.Find(NextPlayerIDValue))
	{
		if (ASeinPlayerStart* Start = FindPlayerStartForSlot(*PreAssigned))
		{
			return Start;
		}
	}

	if (ASeinPlayerStart* Start = FindPlayerStartForSlot(TargetSlot))
	{
		return Start;
	}

	if (ASeinPlayerStart* Start = FindPlayerStartForSlot(0))
	{
		return Start;
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

ASeinPlayerStart* ASeinGameMode::FindPlayerStartForSlot(int32 SlotIndex) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ASeinPlayerStart* FirstUnclaimed = nullptr;

	for (TActorIterator<ASeinPlayerStart> It(World); It; ++It)
	{
		ASeinPlayerStart* Start = *It;

		if (SlotIndex > 0)
		{
			// Looking for a specific slot
			if (Start->PlayerSlot == SlotIndex && !ClaimedSlots.Contains(SlotIndex))
			{
				return Start;
			}
		}
		else
		{
			// Looking for any unclaimed start
			const int32 Slot = Start->PlayerSlot;
			if (Slot == 0 || !ClaimedSlots.Contains(Slot))
			{
				if (!FirstUnclaimed)
				{
					FirstUnclaimed = Start;
				}
			}
		}
	}

	return FirstUnclaimed;
}

// ==================== Player Registration ====================

void ASeinGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	ASeinPlayerController* SeinPC = Cast<ASeinPlayerController>(NewPlayer);
	if (!SeinPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinGameMode: NewPlayer is not ASeinPlayerController. Skipping sim registration."));
		return;
	}

	// Aggressive entry log so the slot-binding race is visible. Captures the
	// PC, the StartSpot ChoosePlayerStart picked, the manifest-resolved flag,
	// and the current ClaimedSlots map. Follow the per-PC log lines back-to-
	// back to see exactly when two PCs collide on the same slot.
	{
		FString ClaimedReport;
		for (const TPair<int32, FSeinPlayerID>& Pair : ClaimedSlots)
		{
			ClaimedReport += FString::Printf(TEXT("%d→%u, "), Pair.Key, Pair.Value.Value);
		}
		AActor* StartSpotActor = SeinPC->StartSpot.Get();
		const ASeinPlayerStart* SeinStartDbg = Cast<ASeinPlayerStart>(StartSpotActor);
		UE_LOG(LogTemp, Log,
			TEXT("SeinGameMode::HandleStartingNewPlayer: PC=%s  bMatchSettingsResolved=%d  StartSpot=%s%s  ClaimedSlots=[%s]"),
			*SeinPC->GetName(),
			(int32)bMatchSettingsResolved,
			*GetNameSafe(StartSpotActor),
			SeinStartDbg ? *FString::Printf(TEXT("(PlayerSlot=%d)"), SeinStartDbg->PlayerSlot) : TEXT(""),
			*ClaimedReport);
	}

	// Match-manifest path: ChoosePlayerStart already routed this controller to a
	// Human slot's SeinPlayerStart. The slot was pre-registered + entity-spawned
	// in BeginPlay — just bind the controller to its PlayerID and bail.
	if (bMatchSettingsResolved)
	{
		if (AActor* StartActor = SeinPC->StartSpot.Get())
		{
			if (ASeinPlayerStart* SeinStart = Cast<ASeinPlayerStart>(StartActor))
			{
				if (SeinStart->PlayerSlot > 0)
				{
					const FSeinPlayerID SlotPlayerID(static_cast<uint8>(SeinStart->PlayerSlot));
					USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
					if (Subsystem && Subsystem->GetPlayerState(SlotPlayerID) != nullptr)
					{
						// COLLISION GUARD: refuse to bind this PC to a slot that's
						// already claimed by a DIFFERENT controller. Without this,
						// two PCs can both end up on the same slot — see the Phase
						// 3b regression where two PCs both claimed slot 3 (Net log
						// "[SLOT COLLISION]" mirror). Drops to legacy fallback for
						// the second PC, which will assign it a fresh slot.
						if (const FSeinPlayerID* AlreadyBound = ClaimedSlots.Find(SeinStart->PlayerSlot))
						{
							UE_LOG(LogTemp, Error,
								TEXT("SeinGameMode: SLOT COLLISION — slot %d is ALREADY claimed by Player(%u) but ChoosePlayerStart routed %s here too. Falling through to legacy fallback (will assign next free PlayerID instead)."),
								SeinStart->PlayerSlot, AlreadyBound->Value, *SeinPC->GetName());
							// fall out of the manifest path
						}
						else
						{
							SeinPC->SeinPlayerID = SlotPlayerID;
							ClaimedSlots.Add(SeinStart->PlayerSlot, SlotPlayerID);
							UE_LOG(LogTemp, Log,
								TEXT("SeinGameMode: Bound %s to pre-registered slot %d (%s)"),
								*SeinPC->GetName(), SeinStart->PlayerSlot, *SlotPlayerID.ToString());

						// Phase 3a: GameMode is the single source of truth for slot
						// binding. Tell USeinNetSubsystem to spawn the lockstep relay
						// for this PC NOW with the slot we just chose, so
						// `Relay->AssignedPlayerID == SeinPC->SeinPlayerID` by
						// construction. The old NetSub auto-spawn (sequential
						// NextSlotToAssign on PostLogin) could disagree with this
						// binding when connect order ≠ slot order.
						if (UGameInstance* GI = GetGameInstance())
						{
							if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
							{
								Net->ServerSpawnRelayForController(SeinPC, SlotPlayerID);
							}
						}
						return;
					}
				}
			}
		}
		}
		// Fell off the manifest path (no SeinPlayerStart, or slot not pre-registered,
		// or slot collision detected above). Drop through to legacy auto-assign so the
		// controller still gets a player ID.
	}

	// Legacy fallback: assign the next free PlayerID, register fresh, spawn the
	// matching SeinPlayerStart's entity. Used when no manifest is configured OR
	// when the controller landed on an Open slot (no pre-registration).
	if (NextPlayerIDValue == 0)
	{
		NextPlayerIDValue = 1; // Skip 0 (Neutral)
	}

	if (NextPlayerIDValue > MaxPlayers)
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinGameMode: Max players (%d) reached. Rejecting player."), MaxPlayers);
		return;
	}

	const FSeinPlayerID NewPlayerID(NextPlayerIDValue);
	NextPlayerIDValue++;

	// Assign to controller
	SeinPC->SeinPlayerID = NewPlayerID;

	// Phase 3a (legacy/fallback path): same single-source-of-truth wiring
	// as the manifest path above — tell NetSubsystem to spawn the relay
	// for this PC with the slot we just chose.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
		{
			Net->ServerSpawnRelayForController(SeinPC, NewPlayerID);
		}
	}

	// Determine faction and team from the player start (if it's a SeinPlayerStart)
	FSeinFactionID FactionForPlayer = DefaultFactionID;
	uint8 TeamForPlayer = 0;
	ASeinPlayerStart* SeinStart = nullptr;

	if (AActor* StartActor = SeinPC->StartSpot.Get())
	{
		SeinStart = Cast<ASeinPlayerStart>(StartActor);
		if (SeinStart)
		{
			// Use faction from the start if set, otherwise use default
			if (SeinStart->FactionID.IsValid())
			{
				FactionForPlayer = SeinStart->FactionID;
			}
			TeamForPlayer = SeinStart->TeamID;

			// Claim this slot
			const int32 Slot = SeinStart->PlayerSlot > 0 ? SeinStart->PlayerSlot : static_cast<int32>(NewPlayerID.Value);
			ClaimedSlots.Add(Slot, NewPlayerID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Assigned %s to %s (Faction %d, Team %d)"),
		*NewPlayerID.ToString(), *SeinPC->GetName(), FactionForPlayer.Value, TeamForPlayer);

	// Register with simulation
	RegisterPlayerWithSim(NewPlayerID, FactionForPlayer, TeamForPlayer);

	// Spawn the start entity if defined on the player start
	if (SeinStart)
	{
		SpawnStartEntity(SeinStart, NewPlayerID);
	}

	// Auto-start simulation on first player. Same defer-to-network rule as
	// PreRegisterMatchSlots above: networked sessions let NetSubsystem start
	// the sim once the local slot is assigned, so the gate is bound first.
	if (bAutoStartSimulation && !bSimulationStarted)
	{
		if (ShouldDeferAutoStartToNetwork(GetWorld()))
		{
			UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Networked session — deferring auto-start to USeinNetSubsystem."));
		}
		else if (USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>())
		{
			Subsystem->StartSimulation();
			bSimulationStarted = true;
			UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Simulation auto-started."));
		}
	}
}

// ==================== Spawn Entity ====================

void ASeinGameMode::SpawnStartEntity(ASeinPlayerStart* PlayerStart, FSeinPlayerID PlayerID)
{
	if (!PlayerStart || !PlayerStart->SpawnEntity)
	{
		return;
	}

	USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinGameMode: No USeinWorldSubsystem. Cannot spawn start entity."));
		return;
	}

	// Read the editor-baked snapshot — see ASeinPlayerStart::PlacedSimLocation.
	// Legacy levels (placed before the snapshot field existed) fall back to
	// runtime FromFloat with a warning. Re-saving the level bakes a fresh
	// snapshot.
	FFixedTransform SpawnTransform;
	if (PlayerStart->bSimLocationBaked)
	{
		SpawnTransform.Location = PlayerStart->PlacedSimLocation;
	}
	else
	{
		const FVector Location = PlayerStart->GetActorLocation();
		UE_LOG(LogTemp, Warning,
			TEXT("SeinGameMode: PlayerStart %s has stale PlacedSimLocation "
				 "(bSimLocationBaked == false) — falling back to runtime "
				 "FromFloat at %s. NOT cross-arch deterministic until the "
				 "level is re-saved."),
			*PlayerStart->GetName(), *Location.ToString());
		SpawnTransform.Location = FFixedVector(
			FFixedPoint::FromFloat(Location.X),
			FFixedPoint::FromFloat(Location.Y),
			FFixedPoint::FromFloat(Location.Z));
	}

	const FSeinEntityHandle Handle = Subsystem->SpawnEntity(PlayerStart->SpawnEntity, SpawnTransform, PlayerID);

	if (Handle.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Spawned start entity %s for %s"),
			*PlayerStart->SpawnEntity->GetName(), *PlayerID.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinGameMode: Failed to spawn start entity %s for %s"),
			*PlayerStart->SpawnEntity->GetName(), *PlayerID.ToString());
	}
}

// ==================== Helpers ====================

void ASeinGameMode::RegisterPlayerWithSim(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID)
{
	USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinGameMode: No USeinWorldSubsystem found. Cannot register player."));
		return;
	}

	Subsystem->RegisterPlayer(PlayerID, FactionID, TeamID);

	// Apply starting resources
	if (FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID))
	{
		for (const auto& Pair : StartingResources)
		{
			State->SetResource(Pair.Key, Pair.Value);
		}
	}
}
