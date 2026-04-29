/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyBPFL.cpp
 */

#include "Lib/SeinLobbyBPFL.h"
#include "ViewModel/SeinLobbyViewModel.h"
#include "Core/SeinUISubsystem.h"
#include "SeinNetSubsystem.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetRelay.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
	UWorld* ResolveWorld(const UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		return WorldContextObject->GetWorld();
	}
}

USeinLobbyViewModel* USeinLobbyBPFL::SeinGetOrCreateLobbyViewModel(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return nullptr;

	USeinUISubsystem* UISub = World->GetSubsystem<USeinUISubsystem>();
	if (!UISub) return nullptr;

	return UISub->GetOrCreateLobbyViewModel();
}

bool USeinLobbyBPFL::SeinRequestSlotClaim(const UObject* WorldContextObject, int32 SlotIndex, FSeinFactionID FactionID)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	// Standalone fast-path: drop directly into the lobby subsystem.
	if (World->GetNetMode() == NM_Standalone)
	{
		USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Lobby || !PC) return false;
		return Lobby->ServerHandleSlotClaim(PC, SlotIndex, FactionID);
	}

	// Networked: find the local relay + send Server_RequestSlotClaim. Same
	// path the `Sein.Net.Lobby.Claim` console command uses.
	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	if (!Net) return false;

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
	{
		ASeinNetRelay* R = Wp.Get();
		if (!R) continue;
		APlayerController* PC = Cast<APlayerController>(R->GetOwner());
		if (PC && PC->IsLocalController())
		{
			R->Server_RequestSlotClaim(SlotIndex, FactionID);
			return true;
		}
	}
	return false;
}

bool USeinLobbyBPFL::SeinRequestStartMatch(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return false;

	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_Client)
	{
		// Clients cannot start a match. Designers should hide / disable the
		// button on non-host clients via the view model's IsHost() check.
		return false;
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;

	USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
	if (!Lobby) return false;

	const bool bTravel = !Lobby->GameplayMap.IsNull();
	return Lobby->ServerStartMatch(bTravel);
}

void USeinLobbyBPFL::SeinRequestLeaveLobby(const UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World) return;
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return;
	// ConsoleCommand("disconnect") is the cleanest cross-mode disconnect:
	// host kicks the listen-server, client drops its connection. Project
	// hooks main-menu return via UE's standard NetworkFailure / MapTravel
	// delegates if a custom return-to-menu path is needed.
	PC->ConsoleCommand(TEXT("disconnect"));
}
