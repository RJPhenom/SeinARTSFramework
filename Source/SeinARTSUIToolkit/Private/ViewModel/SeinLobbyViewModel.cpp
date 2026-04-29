/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyViewModel.cpp
 */

#include "ViewModel/SeinLobbyViewModel.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Containers/Ticker.h"

void USeinLobbyViewModel::Initialize(UWorld* InWorld)
{
	if (!InWorld) return;
	CachedWorld = InWorld;

	if (USeinLobbySubsystem* Lobby = GetLobbySubsystem())
	{
		if (ASeinLobbyState* Actor = Lobby->GetLobbyState())
		{
			BoundActor = Actor;
			LobbyChangedHandle = Actor->OnLobbyStateChanged.AddUObject(this, &USeinLobbyViewModel::HandleLobbyStateChanged);
			RefreshFromActor();
		}
	}

	// The lobby actor may not exist on the client until replication arrives.
	// Poll once per second on the GT until it does, then bind. Cheap; cleared
	// on Shutdown. Could be a delegate from the subsystem instead — kept simple
	// for now.
	LocalSlotPollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			HandleLocalSlotPoll();

			// Late-bind the actor if it just arrived via replication.
			if (!BoundActor.IsValid())
			{
				if (USeinLobbySubsystem* Lobby = GetLobbySubsystem())
				{
					if (ASeinLobbyState* Actor = Lobby->GetLobbyState())
					{
						BoundActor = Actor;
						LobbyChangedHandle = Actor->OnLobbyStateChanged.AddUObject(this, &USeinLobbyViewModel::HandleLobbyStateChanged);
						RefreshFromActor();
					}
				}
			}

			return true; // keep ticking
		}), 1.0f);
}

void USeinLobbyViewModel::Shutdown()
{
	if (BoundActor.IsValid() && LobbyChangedHandle.IsValid())
	{
		BoundActor->OnLobbyStateChanged.Remove(LobbyChangedHandle);
	}
	LobbyChangedHandle.Reset();

	if (LocalSlotPollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LocalSlotPollHandle);
		LocalSlotPollHandle.Reset();
	}

	BoundActor.Reset();
	CachedSlots.Reset();
	LocalSlotID = FSeinPlayerID::Neutral();
	CachedWorld.Reset();
}

bool USeinLobbyViewModel::TryGetSlot(int32 SlotIndex, FSeinLobbySlotState& OutSlot) const
{
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.SlotIndex == SlotIndex)
		{
			OutSlot = Slot;
			return true;
		}
	}
	return false;
}

bool USeinLobbyViewModel::IsHost() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_ListenServer || Mode == NM_DedicatedServer || Mode == NM_Standalone;
}

bool USeinLobbyViewModel::CanStartMatch() const
{
	if (!IsHost()) return false;
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.bClaimed && Slot.State == ESeinSlotState::Human)
		{
			return true;
		}
	}
	return false;
}

bool USeinLobbyViewModel::HasPublishedMatchSnapshot() const
{
	const USeinLobbySubsystem* Lobby = GetLobbySubsystem();
	return Lobby && Lobby->HasPublishedSnapshot();
}

USeinLobbySubsystem* USeinLobbyViewModel::GetLobbySubsystem() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
}

void USeinLobbyViewModel::RefreshFromActor()
{
	const ASeinLobbyState* Actor = BoundActor.Get();
	if (!Actor)
	{
		CachedSlots.Reset();
	}
	else
	{
		CachedSlots = Actor->Slots;
	}
	OnLobbyChanged.Broadcast();
}

void USeinLobbyViewModel::HandleLobbyStateChanged()
{
	RefreshFromActor();
}

void USeinLobbyViewModel::HandleLocalSlotPoll()
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return;

	const USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
	if (!Net) return;

	const FSeinPlayerID NewSlot = Net->GetLocalPlayerID();
	if (NewSlot != LocalSlotID)
	{
		LocalSlotID = NewSlot;
		OnLocalSlotChanged.Broadcast(LocalSlotID);
	}
}
