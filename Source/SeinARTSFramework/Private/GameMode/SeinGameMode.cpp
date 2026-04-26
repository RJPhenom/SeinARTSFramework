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
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ASeinGameMode::ASeinGameMode()
{
	DefaultPawnClass = ASeinCameraPawn::StaticClass();
	PlayerControllerClass = ASeinPlayerController::StaticClass();
	HUDClass = ASeinHUD::StaticClass();
}

void ASeinGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

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

	// TODO: chain GameInstance runtime override + plugin-settings PIE default
	// before this lookup once those land.
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
	if (bAutoStartSimulation && !bSimulationStarted)
	{
		Subsystem->StartSimulation();
		bSimulationStarted = true;
		UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Simulation auto-started after slot pre-registration"));
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
		for (const FSeinMatchSlot& Slot : ResolvedMatchSettings.Slots)
		{
			if (Slot.State != ESeinSlotState::Human) continue;
			if (ClaimedSlots.Contains(Slot.SlotIndex)) continue;

			for (TActorIterator<ASeinPlayerStart> It(GetWorld()); It; ++It)
			{
				if (It->PlayerSlot == Slot.SlotIndex)
				{
					return *It;
				}
			}
		}

		// Manifest exhausted (lobby filled / over capacity). Fall back to base
		// class so the controller still gets a default spawn somewhere.
		UE_LOG(LogTemp, Warning,
			TEXT("SeinGameMode: No available Human slot for new controller — using base PlayerStart"));
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
						SeinPC->SeinPlayerID = SlotPlayerID;
						ClaimedSlots.Add(SeinStart->PlayerSlot, SlotPlayerID);
						UE_LOG(LogTemp, Log,
							TEXT("SeinGameMode: Bound %s to pre-registered slot %d (%s)"),
							*SeinPC->GetName(), SeinStart->PlayerSlot, *SlotPlayerID.ToString());
						return;
					}
				}
			}
		}
		// Fell off the manifest path (no SeinPlayerStart, or slot not pre-registered).
		// Drop through to legacy auto-assign so the controller still gets a player ID.
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

	// Auto-start simulation on first player
	if (bAutoStartSimulation && !bSimulationStarted)
	{
		if (USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>())
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
