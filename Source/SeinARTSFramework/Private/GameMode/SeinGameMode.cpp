/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.cpp
 * @brief   RTS game mode implementation.
 */

#include "GameMode/SeinGameMode.h"
#include "GameMode/SeinPlayerStart.h"
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
}

// ==================== Player Start Selection ====================

AActor* ASeinGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// If we have a SeinPlayerStart for the next slot, use it.
	// Otherwise fall back to default behavior.
	const int32 TargetSlot = static_cast<int32>(NextPlayerIDValue);

	// Check if lobby pre-assigned a slot for this player ID
	if (const int32* PreAssigned = PreAssignedSlots.Find(NextPlayerIDValue))
	{
		if (ASeinPlayerStart* Start = FindPlayerStartForSlot(*PreAssigned))
		{
			return Start;
		}
	}

	// Auto-assign: try to find a start matching the next player slot
	if (ASeinPlayerStart* Start = FindPlayerStartForSlot(TargetSlot))
	{
		return Start;
	}

	// Try any unclaimed start (slot 0 = any)
	if (ASeinPlayerStart* Start = FindPlayerStartForSlot(0))
	{
		return Start;
	}

	// No SeinPlayerStart found — fall back to base class (standard APlayerStart)
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

	// Assign player ID
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
	SeinPC->DispatchMode = DefaultDispatchMode;

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

	const FVector Location = PlayerStart->GetActorLocation();
	const FRotator Rotation = PlayerStart->GetActorRotation();

	// Build a fixed-point transform from the player start's world transform
	FFixedTransform SpawnTransform;
	SpawnTransform.Location = FFixedVector(
		FFixedPoint::FromFloat(Location.X),
		FFixedPoint::FromFloat(Location.Y),
		FFixedPoint::FromFloat(Location.Z));

	const FSeinEntityHandle Handle = Subsystem->SpawnEntity(PlayerStart->SpawnEntity, SpawnTransform, PlayerID);

	if (Handle.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("SeinGameMode: Spawned start entity %s for %s at %s"),
			*PlayerStart->SpawnEntity->GetName(), *PlayerID.ToString(), *Location.ToString());
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
			State->SetResource(Pair.Key, FFixedPoint::FromFloat(Pair.Value));
		}
	}
}
