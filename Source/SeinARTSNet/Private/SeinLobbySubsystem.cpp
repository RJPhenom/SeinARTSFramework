/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbySubsystem.cpp
 */

#include "SeinLobbySubsystem.h"
#include "SeinARTSNet.h"
#include "SeinLobbyState.h"
#include "SeinNetSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

void USeinLobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &USeinLobbySubsystem::OnPostLogin);
	LogoutHandle    = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &USeinLobbySubsystem::OnLogout);

	UE_LOG(LogSeinNet, Log, TEXT("USeinLobbySubsystem initialized."));
}

void USeinLobbySubsystem::Deinitialize()
{
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);

	LobbyStateActor.Reset();
	ControllerToSlot.Reset();
	PublishedSnapshot = FSeinMatchSettings();
	bSnapshotPublished = false;

	UE_LOG(LogSeinNet, Log, TEXT("USeinLobbySubsystem deinitialized."));
	Super::Deinitialize();
}

bool USeinLobbySubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	// Standalone counts as "server" for lobby purposes — single-player builds
	// still benefit from a slot manifest if a designer authored one. The lobby
	// just won't have any networked claims.
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer || Mode == NM_Standalone;
}

void USeinLobbySubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer) return;
	if (!IsServer()) return;

	UWorld* World = GameMode->GetWorld();
	if (!World) return;
	if (World->GetGameInstance() != GetGameInstance())
	{
		// A different GI's game mode (sub-PIE world) — ignore.
		return;
	}

	// Lazy-init: spawn the lobby actor + seed slots on first PostLogin. Picks
	// MaxPlayers from settings as the default seat count. Designers wanting a
	// different lobby size will call InitializeLobby explicitly from a Widget BP
	// (Phase 3c) before any PostLogin fires; if so, this no-ops.
	EnsureLobbyActor();

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	// Auto-claim: route the joining controller to the next free Open slot.
	// This MIRRORS the ServerSpawnRelayForController flow that GameMode runs in
	// HandleStartingNewPlayer — we want lobby state to track the same slot the
	// game mode is going to use. Keep the convention simple (slot N → PlayerID N)
	// so the lobby is a faithful pre-image of the runtime slot manifest.
	const int32 FreeSlot = PickNextFreeSlot();
	if (FreeSlot <= 0)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] OnPostLogin: no free slot for %s — lobby is full (%d slot(s))."),
			*GetNameSafe(NewPlayer), Actor->Slots.Num());
		return;
	}

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(FreeSlot);
	if (!Slot) return;

	Slot->State = ESeinSlotState::Human;
	Slot->bClaimed = true;
	Slot->ClaimedBy = FSeinPlayerID(static_cast<uint8>(FreeSlot));
	// Treat the seeded "Open" placeholder as empty — same logic as the cross-slot
	// claim path. Without this, the three auto-claimed slots stay labeled "Open"
	// in the replicated state because the seed wrote that placeholder first.
	if (Slot->DisplayName.IsEmpty() ||
		Slot->DisplayName.EqualTo(FText::FromString(TEXT("Open"))))
	{
		Slot->DisplayName = FText::FromString(FString::Printf(TEXT("Player %d"), FreeSlot));
	}

	ControllerToSlot.Add(TWeakObjectPtr<APlayerController>(NewPlayer), FreeSlot);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] OnPostLogin: claimed slot %d for %s (auto-assigned)."),
		FreeSlot, *GetNameSafe(NewPlayer));

	CommitSlotState(FreeSlot, *Slot);
}

void USeinLobbySubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!Exiting || !IsServer()) return;

	APlayerController* PC = Cast<APlayerController>(Exiting);
	if (!PC) return;

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	// Release the slot owned by the leaving controller.
	int32 ReleasedSlot = 0;
	for (auto It = ControllerToSlot.CreateIterator(); It; ++It)
	{
		if (It.Key().Get() == PC)
		{
			ReleasedSlot = It.Value();
			It.RemoveCurrent();
			break;
		}
	}
	if (ReleasedSlot <= 0) return;

	if (FSeinLobbySlotState* Slot = Actor->FindSlotMutable(ReleasedSlot))
	{
		Slot->State = ESeinSlotState::Open;
		Slot->bClaimed = false;
		Slot->ClaimedBy = FSeinPlayerID::Neutral();
		Slot->DisplayName = FText::FromString(TEXT("Open"));
		// Keep FactionID + TeamID so re-joiners can see what was previously
		// configured here; lobby UI may want to clear them. Phase 3c decision.

		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] OnLogout: released slot %d (was %s)."),
			ReleasedSlot, *GetNameSafe(PC));

		CommitSlotState(ReleasedSlot, *Slot);
	}
}

void USeinLobbySubsystem::InitializeLobby(int32 SlotCount)
{
	if (!IsServer()) return;

	if (SlotCount <= 0)
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		SlotCount = (Settings && Settings->MaxPlayers > 0) ? Settings->MaxPlayers : 4;
	}

	EnsureLobbyActor();
	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	Actor->Slots.Reset();
	Actor->Slots.Reserve(SlotCount);
	for (int32 i = 1; i <= SlotCount; ++i)
	{
		FSeinLobbySlotState S;
		S.SlotIndex = i;
		S.State = ESeinSlotState::Open;
		S.DisplayName = FText::FromString(TEXT("Open"));
		Actor->Slots.Add(S);
	}

	ControllerToSlot.Reset();

	// MarkDirty so the freshly-seeded array replicates immediately.
	Actor->ForceNetUpdate();
	Actor->OnLobbyStateChanged.Broadcast();

	UE_LOG(LogSeinNet, Log, TEXT("[Lobby] InitializeLobby: seeded %d Open slot(s)."), SlotCount);
}

bool USeinLobbySubsystem::ServerHandleSlotClaim(APlayerController* PC, int32 SlotIndex, FSeinFactionID Faction)
{
	if (!IsServer() || !PC) return false;

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerHandleSlotClaim: no lobby actor."));
		return false;
	}

	FSeinLobbySlotState* Target = Actor->FindSlotMutable(SlotIndex);
	if (!Target)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d does not exist (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	if (Target->State == ESeinSlotState::Closed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d is Closed (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	// Identify the requesting controller's CURRENT slot (if any).
	int32 CurrentSlot = 0;
	for (const auto& Pair : ControllerToSlot)
	{
		if (Pair.Key.Get() == PC)
		{
			CurrentSlot = Pair.Value;
			break;
		}
	}

	if (CurrentSlot == SlotIndex)
	{
		// Same-slot re-claim: just update the faction.
		Target->FactionID = Faction;
		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] ServerHandleSlotClaim: %s updated faction on slot %d → %u"),
			*GetNameSafe(PC), SlotIndex, Faction.Value);
		CommitSlotState(SlotIndex, *Target);
		return true;
	}

	// Cross-slot move: target must be free (Open + not claimed) OR a same-team
	// reassignment by the slot's current owner. For now: accept only Open+unclaimed.
	if (Target->bClaimed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d already claimed (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	// Release current slot if PC had one.
	if (CurrentSlot > 0)
	{
		if (FSeinLobbySlotState* Old = Actor->FindSlotMutable(CurrentSlot))
		{
			Old->State = ESeinSlotState::Open;
			Old->bClaimed = false;
			Old->ClaimedBy = FSeinPlayerID::Neutral();
			Old->DisplayName = FText::FromString(TEXT("Open"));
			CommitSlotState(CurrentSlot, *Old);
		}
		ControllerToSlot.Remove(TWeakObjectPtr<APlayerController>(PC));
	}

	Target->State = ESeinSlotState::Human;
	Target->bClaimed = true;
	Target->ClaimedBy = FSeinPlayerID(static_cast<uint8>(SlotIndex));
	Target->FactionID = Faction;
	if (Target->DisplayName.IsEmpty() ||
		Target->DisplayName.EqualTo(FText::FromString(TEXT("Open"))))
	{
		Target->DisplayName = FText::FromString(FString::Printf(TEXT("Player %d"), SlotIndex));
	}

	ControllerToSlot.Add(TWeakObjectPtr<APlayerController>(PC), SlotIndex);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSlotClaim: %s claimed slot %d  faction=%u"),
		*GetNameSafe(PC), SlotIndex, Faction.Value);

	CommitSlotState(SlotIndex, *Target);
	return true;
}

bool USeinLobbySubsystem::BuildMatchSettingsSnapshot(FSeinMatchSettings& CapturedSettingsOut) const
{
	CapturedSettingsOut = FSeinMatchSettings();
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	int32 PopulatedSlots = 0;
	CapturedSettingsOut.Slots.Reserve(Actor->Slots.Num());
	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		FSeinMatchSlot Out;
		Out.SlotIndex = Slot.SlotIndex;
		Out.State = Slot.State;
		Out.FactionID = Slot.FactionID;
		Out.TeamID = Slot.TeamID;
		Out.DisplayName = Slot.DisplayName;
		Out.AIProfile = Slot.AIProfile;
		CapturedSettingsOut.Slots.Add(Out);

		if (Slot.State == ESeinSlotState::Human || Slot.State == ESeinSlotState::AI)
		{
			++PopulatedSlots;
		}
	}

	return PopulatedSlots > 0;
}

void USeinLobbySubsystem::PublishMatchSettingsSnapshot()
{
	if (!IsServer()) return;

	FSeinMatchSettings Snap;
	const bool bMeaningful = BuildMatchSettingsSnapshot(Snap);
	if (!bMeaningful)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] PublishMatchSettingsSnapshot: lobby has no Human/AI slots — publishing empty snapshot (game mode will fall back to WorldSettings)."));
	}

	PublishedSnapshot = MoveTemp(Snap);
	bSnapshotPublished = true;

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] PublishMatchSettingsSnapshot: %d slot(s) captured → published as GI override."),
		PublishedSnapshot.Slots.Num());
}

bool USeinLobbySubsystem::ServerStartMatch(bool bTravelToGameplayMap)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerStartMatch: rejected — caller is not server."));
		return false;
	}

	// Validate: must have at least one Human-claimed slot.
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	bool bHasHuman = false;
	if (Actor)
	{
		for (const FSeinLobbySlotState& Slot : Actor->Slots)
		{
			if (Slot.bClaimed && Slot.State == ESeinSlotState::Human)
			{
				bHasHuman = true;
				break;
			}
		}
	}
	if (!bHasHuman)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerStartMatch: rejected — no Human-claimed slot in lobby. Claim a slot first (Sein.Net.Lobby.Claim or via UI)."));
		return false;
	}

	// Snapshot the lobby state into the GI override so whichever GameMode
	// runs next (current world OR the post-travel world) picks it up.
	PublishMatchSettingsSnapshot();

	if (bTravelToGameplayMap)
	{
		if (GameplayMap.IsNull())
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Lobby] ServerStartMatch: travel requested but GameplayMap is empty — falling through to in-place start."));
		}
		else
		{
			UWorld* World = GetWorld();
			if (World)
			{
				const FString MapPath = GameplayMap.ToString();
				UE_LOG(LogSeinNet, Log,
					TEXT("[Lobby] ServerStartMatch: travel to %s"), *MapPath);
				World->ServerTravel(MapPath, /*bAbsolute=*/true);
				return true;
			}
		}
	}

	// In-place start (no travel, or travel mis-configured): kick the lockstep
	// session right here in the current world.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
		{
			UE_LOG(LogSeinNet, Log, TEXT("[Lobby] ServerStartMatch: starting lockstep in-place."));
			Net->StartLockstepSession();
			return true;
		}
	}

	UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerStartMatch: USeinNetSubsystem missing — could not start."));
	return false;
}

void USeinLobbySubsystem::NotifyLobbyStateActorBeginPlay(ASeinLobbyState* Actor)
{
	if (!Actor) return;
	LobbyStateActor = Actor;
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Lobby] NotifyLobbyStateActorBeginPlay: latched %s (NetMode=%d)"),
		*GetNameSafe(Actor), (int32)Actor->GetNetMode());
}

void USeinLobbySubsystem::NotifyLobbyStateActorEndPlay(ASeinLobbyState* Actor)
{
	if (LobbyStateActor.Get() == Actor)
	{
		LobbyStateActor.Reset();
		UE_LOG(LogSeinNet, Verbose, TEXT("[Lobby] NotifyLobbyStateActorEndPlay: cleared LobbyStateActor."));
	}
}

void USeinLobbySubsystem::EnsureLobbyActor()
{
	if (!IsServer()) return;
	if (LobbyStateActor.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Params.bAllowDuringConstructionScript = true;

	ASeinLobbyState* Actor = World->SpawnActor<ASeinLobbyState>(ASeinLobbyState::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		UE_LOG(LogSeinNet, Error, TEXT("[Lobby] EnsureLobbyActor: SpawnActor returned null."));
		return;
	}

	LobbyStateActor = Actor;

	// Seed a default-sized lobby. InitializeLobby is idempotent in the wipe
	// sense — calling it after PostLogin has already placed players would
	// clobber claims. So only seed if the slot array is empty (ie this is
	// the lazy-init path).
	if (Actor->Slots.IsEmpty())
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const int32 SlotCount = (Settings && Settings->MaxPlayers > 0) ? Settings->MaxPlayers : 4;
		Actor->Slots.Reserve(SlotCount);
		for (int32 i = 1; i <= SlotCount; ++i)
		{
			FSeinLobbySlotState S;
			S.SlotIndex = i;
			S.State = ESeinSlotState::Open;
			S.DisplayName = FText::FromString(TEXT("Open"));
			Actor->Slots.Add(S);
		}
		Actor->ForceNetUpdate();
		UE_LOG(LogSeinNet, Log, TEXT("[Lobby] EnsureLobbyActor: spawned actor + seeded %d Open slot(s)."), SlotCount);
	}
}

int32 USeinLobbySubsystem::PickNextFreeSlot() const
{
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return 0;
	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		if (Slot.State != ESeinSlotState::Open) continue;
		if (Slot.bClaimed) continue;
		return Slot.SlotIndex;
	}
	return 0;
}

void USeinLobbySubsystem::CommitSlotState(int32 SlotIndex, const FSeinLobbySlotState& NewState)
{
	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	// Mutating the array in-place was already done by callers (FindSlotMutable
	// returns a live pointer); this method is the post-commit broadcast +
	// replication-poke. Kept as a separate hook so future Phase 3c additions
	// (per-slot RepNotify, faction validation) have a single chokepoint.
	Actor->ForceNetUpdate();
	Actor->OnLobbyStateChanged.Broadcast();

	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Lobby] CommitSlotState: slot %d  State=%d  bClaimed=%d  Faction=%u  ClaimedBy=%u"),
		SlotIndex, (int32)NewState.State, (int32)NewState.bClaimed,
		NewState.FactionID.Value, NewState.ClaimedBy.Value);
}
