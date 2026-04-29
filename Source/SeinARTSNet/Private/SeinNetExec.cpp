/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetExec.cpp
 * @brief   Phase 1 console hooks for proving the lockstep wire end-to-end in PIE.
 *
 *   Sein.Net.TestPing [TurnId]   — local-PC submit of a single Ping command.
 *                                  Optional TurnId for testing the completeness
 *                                  gate (run on every window with the same
 *                                  TurnId to see one fan-out). With no arg,
 *                                  uses an auto-incrementing local counter.
 *
 *   Sein.Net.Status              — dump NetMode, networking-active flag,
 *                                  LocalPlayerID, SessionSeed, slot/relay counts.
 *
 * Drive these from the in-PIE console (~). Watch LogSeinNet at Verbose to see
 * each hop. Every PIE window is independent — fire on each window with the
 * same TurnId to demonstrate the completeness gate (server holds until every
 * connected slot has submitted).
 */

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

#include "SeinARTSNet.h"
#include "SeinNetSubsystem.h"
#include "SeinNetRelay.h"
#include "SeinReplayWriter.h"
#include "SeinReplayReader.h"
#include "SeinLobbySubsystem.h"
#include "SeinLobbyState.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinEntityPool.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "HAL/FileManager.h"
#include "Types/Vector.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"

namespace
{
	static int32 GTestPingTurnCounter = 0;

	void HandleTestPing(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: no World."));
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		if (!GI)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: no GameInstance."));
			return;
		}
		USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>();
		if (!Net)
		{
			Ar.Log(TEXT("[SeinNet] TestPing: USeinNetSubsystem missing."));
			return;
		}

		// Resolve TurnId: explicit arg if given, else auto-incrementing local
		// counter. To exercise the completeness gate, run with the SAME TurnId
		// on every PIE window: server holds the assembly until every slot has
		// submitted, then fans out exactly once.
		int32 TurnId = 0;
		if (Args.Num() > 0)
		{
			TurnId = FCString::Atoi(*Args[0]);
		}
		else
		{
			TurnId = ++GTestPingTurnCounter;
		}

		// Build a Ping command stamped with the local slot. Server overwrites
		// PlayerID anyway from the source relay's AssignedPlayerID, but a
		// correct local stamp keeps the local log readable pre-network.
		const FSeinPlayerID Slot = Net->GetLocalPlayerID();
		const FFixedVector FakeLoc = FFixedVector();
		const FSeinCommand Cmd = FSeinCommand::MakePingCommand(Slot, FakeLoc);

		Ar.Logf(TEXT("[SeinNet] TestPing: TurnId=%d  LocalSlot=%u  Active=%d  submitting..."),
			TurnId, Slot.Value, (int32)Net->IsNetworkingActive());
		Net->SubmitLocalCommandAtTurn(TurnId, Cmd);
	}

	void HandleStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] Status: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;

		if (!Net)
		{
			Ar.Logf(TEXT("[SeinNet] Status: NetMode=%d  USeinNetSubsystem MISSING."), (int32)World->GetNetMode());
			return;
		}

		Ar.Logf(TEXT("[SeinNet] Status:"));
		Ar.Logf(TEXT("  NetMode             = %d"), (int32)World->GetNetMode());
		Ar.Logf(TEXT("  NetworkingActive    = %d"), (int32)Net->IsNetworkingActive());
		Ar.Logf(TEXT("  LocalPlayerID       = %u  (0 = not yet assigned)"), Net->GetLocalPlayerID().Value);
		Ar.Logf(TEXT("  SessionSeed         = %lld"), Net->GetSessionSeed());
		Ar.Logf(TEXT("  RegisteredRelays    = %d"), Net->GetRelays().Num());
		Ar.Logf(TEXT("  ActiveSlots(server) = %d"), Net->GetActiveSlotCount());
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdTestPing(
		TEXT("Sein.Net.TestPing"),
		TEXT("Sein.Net.TestPing [TurnId] — submit a Ping command for the given TurnId (default: auto-increment). Use the same TurnId on every window to test the completeness gate."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleTestPing));

	void HandleStartMatch(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] StartMatch: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] StartMatch: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Logf(TEXT("[SeinNet] StartMatch: must be run on the SERVER (NetMode=%d). Run on the host (Listen Server) window."), (int32)Mode);
			return;
		}

		// Phase 3b: snapshot the lobby BEFORE the lockstep session starts so
		// any future map-travel / GameMode resolve picks up the captured slot
		// manifest (faction picks, claims, AI policies). Lobby snapshot is a
		// no-op if no lobby actor exists; safe to call always.
		if (USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
		{
			Lobby->PublishMatchSettingsSnapshot();
			const FSeinMatchSettings& Snap = Lobby->GetPublishedSnapshot();
			Ar.Logf(TEXT("[SeinNet] StartMatch: captured lobby snapshot (%d slot(s))."), Snap.Slots.Num());
		}
		else
		{
			Ar.Log(TEXT("[SeinNet] StartMatch: USeinLobbySubsystem missing — proceeding without snapshot."));
		}

		Ar.Logf(TEXT("[SeinNet] StartMatch: triggering lockstep session start across %d relay(s)."), Net->GetRelays().Num());
		Net->StartLockstepSession();
	}

	// ============== Lobby commands (Phase 3b) ==============

	const TCHAR* SlotStateToString(ESeinSlotState S)
	{
		switch (S)
		{
		case ESeinSlotState::Open:   return TEXT("Open");
		case ESeinSlotState::Human:  return TEXT("Human");
		case ESeinSlotState::AI:     return TEXT("AI");
		case ESeinSlotState::Closed: return TEXT("Closed");
		default:                     return TEXT("?");
		}
	}

	void HandleLobbyStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Status: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinLobbySubsystem* Lobby = GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
		if (!Lobby) { Ar.Log(TEXT("[SeinLobby] Status: USeinLobbySubsystem missing.")); return; }

		ASeinLobbyState* Actor = Lobby->GetLobbyState();
		if (!Actor)
		{
			Ar.Logf(TEXT("[SeinLobby] No lobby actor yet (NetMode=%d). On clients this means initial replication hasn't arrived."),
				(int32)World->GetNetMode());
			return;
		}

		Ar.Logf(TEXT("[SeinLobby] Lobby state (%d slot(s), NetMode=%d, SnapshotPublished=%d):"),
			Actor->Slots.Num(), (int32)World->GetNetMode(), (int32)Lobby->HasPublishedSnapshot());
		for (const FSeinLobbySlotState& Slot : Actor->Slots)
		{
			Ar.Logf(TEXT("  slot=%d  state=%s  bClaimed=%d  ClaimedBy=%u  Faction=%u  Team=%u  Name=\"%s\""),
				Slot.SlotIndex,
				SlotStateToString(Slot.State),
				(int32)Slot.bClaimed,
				Slot.ClaimedBy.Value,
				Slot.FactionID.Value,
				Slot.TeamID,
				*Slot.DisplayName.ToString());
		}
	}

	void HandleLobbyClaim(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Claim: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinLobby] Claim: usage: Sein.Net.Lobby.Claim <SlotIndex> [FactionID]"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		const int32 Faction = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
		if (Slot <= 0 || Slot > 255)
		{
			Ar.Logf(TEXT("[SeinLobby] Claim: SlotIndex must be 1..255 (got %d)."), Slot);
			return;
		}
		if (Faction < 0 || Faction > 255)
		{
			Ar.Logf(TEXT("[SeinLobby] Claim: FactionID must be 0..255 (got %d)."), Faction);
			return;
		}

		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinLobby] Claim: USeinNetSubsystem missing.")); return; }

		// Standalone fast-path: drop the call directly into the lobby subsystem
		// using whichever local PC is available.
		if (World->GetNetMode() == NM_Standalone)
		{
			USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>();
			APlayerController* PC = World->GetFirstPlayerController();
			if (!Lobby || !PC)
			{
				Ar.Log(TEXT("[SeinLobby] Claim [Standalone]: no Lobby or PC."));
				return;
			}
			const bool bOk = Lobby->ServerHandleSlotClaim(PC, Slot, FSeinFactionID(static_cast<uint8>(Faction)));
			Ar.Logf(TEXT("[SeinLobby] Claim [Standalone]: slot=%d faction=%d  result=%s"),
				Slot, Faction, bOk ? TEXT("OK") : TEXT("REJECTED"));
			return;
		}

		// Networked: route via the local relay → Server_RequestSlotClaim.
		ASeinNetRelay* Relay = nullptr;
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
		{
			ASeinNetRelay* R = Wp.Get();
			if (!R) continue;
			APlayerController* PC = Cast<APlayerController>(R->GetOwner());
			if (PC && PC->IsLocalController())
			{
				Relay = R;
				break;
			}
		}
		if (!Relay)
		{
			Ar.Log(TEXT("[SeinLobby] Claim: no local relay yet (PostLogin not reached?)."));
			return;
		}

		Ar.Logf(TEXT("[SeinLobby] Claim: requesting slot=%d faction=%d via relay %s"),
			Slot, Faction, *GetNameSafe(Relay));
		Relay->Server_RequestSlotClaim(Slot, FSeinFactionID(static_cast<uint8>(Faction)));
	}

	void HandleLobbyInit(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinLobby] Init: no World.")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer && Mode != NM_Standalone)
		{
			Ar.Logf(TEXT("[SeinLobby] Init: SERVER ONLY (NetMode=%d)."), (int32)Mode);
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		USeinLobbySubsystem* Lobby = GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
		if (!Lobby) { Ar.Log(TEXT("[SeinLobby] Init: USeinLobbySubsystem missing.")); return; }

		const int32 SlotCount = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		Lobby->InitializeLobby(SlotCount);
		Ar.Logf(TEXT("[SeinLobby] Init: re-seeded with %d Open slot(s) (0 = use settings default)."),
			SlotCount);
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyStatus(
		TEXT("Sein.Net.Lobby.Status"),
		TEXT("Dump current lobby slot state. Run on each PIE window: server is authoritative, clients mirror via replication."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyStatus));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyClaim(
		TEXT("Sein.Net.Lobby.Claim"),
		TEXT("Sein.Net.Lobby.Claim <SlotIndex> [FactionID] — request the given slot for the local player. Faction defaults to 0 (unset). Server validates."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyClaim));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLobbyInit(
		TEXT("Sein.Net.Lobby.Init"),
		TEXT("SERVER ONLY. Sein.Net.Lobby.Init [SlotCount] — re-seed the lobby with N Open slots (default = settings.MaxPlayers). Wipes existing claims."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLobbyInit));

	// ============== Determinism gossip / desync (Phase 4) ==============

	void HandleSimulateDesync(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateDesync: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateDesync: USeinNetSubsystem missing.")); return; }

		// Build a fake hash table where every peer disagrees, so the alarm
		// path exercises end-to-end without actually corrupting the sim.
		// Every slot gets a distinct hash; the comparison fails immediately.
		const int32 FakeTurn = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 999;

		TArray<FSeinSlotHashEntry> Fake;
		const auto& Relays = Net->GetRelays();
		uint32 Counter = 0xDEAD0000u;
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
		{
			if (ASeinNetRelay* R = Wp.Get())
			{
				Fake.Emplace(R->AssignedPlayerID, Counter++);
			}
		}
		if (Fake.IsEmpty())
		{
			Fake.Emplace(Net->GetLocalPlayerID(), 0xDEADBEEFu);
			Fake.Emplace(FSeinPlayerID(255), 0xCAFEBABEu);
		}

		Ar.Logf(TEXT("[SeinNet] SimulateDesync: triggering local alarm path with %d fake peer hashes (Turn=%d)."),
			Fake.Num(), FakeTurn);
		Net->ClientHandleDesyncNotification(FakeTurn, Fake);
	}

	void HandleClearDesync(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] ClearDesync: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] ClearDesync: USeinNetSubsystem missing.")); return; }

		// We can't directly write Net->bDesyncDetected (it's private). Use
		// GEngine to clear all on-screen messages — designer can use this
		// to silence the red alarm without restarting PIE. The flag stays
		// set internally; restart PIE to fully clear.
		if (GEngine)
		{
			GEngine->ClearOnScreenDebugMessages();
		}
		Ar.Log(TEXT("[SeinNet] ClearDesync: cleared on-screen alarm (note: bDesyncDetected flag remains set in subsystem until PIE restart)."));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateDesync(
		TEXT("Sein.Net.SimulateDesync"),
		TEXT("Sein.Net.SimulateDesync [Turn] — fire the LOCAL desync alarm with fake per-peer hashes for testing the red on-screen UI. Does not actually desync the sim. Default Turn=999."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateDesync));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdClearDesync(
		TEXT("Sein.Net.ClearDesync"),
		TEXT("Clear the red on-screen desync alarm message. Internal bDesyncDetected flag stays set until PIE restart."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleClearDesync));

	// ============== Replay reader (Phase 4a) ==============

	void HandleLoadReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LoadReplay: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: usage: Sein.Net.LoadReplay <FileNameOrPath>  (resolves bare filenames against Saved/Replays/)."));
			return;
		}
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] LoadReplay: USeinNetSubsystem missing.")); return; }

		USeinReplayReader* Reader = Net->GetOrCreateReplayReader();
		if (!Reader) { Ar.Log(TEXT("[SeinNet] LoadReplay: failed to create reader.")); return; }
		if (Reader->IsPlaying())
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: a replay is already playing — call Sein.Net.StopReplay first."));
			return;
		}

		const FString& Path = Args[0];
		Ar.Logf(TEXT("[SeinNet] LoadReplay: loading %s..."), *Path);
		if (!Reader->LoadFromFile(Path))
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: failed (see log for details)."));
			return;
		}

		const FSeinReplayHeader& H = Reader->GetHeader();
		Ar.Logf(TEXT("[SeinNet] LoadReplay: loaded %d turn(s)  seed=%lld  map=%s  recorded=%s"),
			Reader->GetTurnCount(), H.RandomSeed, *H.MapIdentifier.ToString(), *H.RecordedAt.ToString());

		if (Reader->Play())
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: playback started. Use Sein.Net.StopReplay to halt."));
		}
		else
		{
			Ar.Log(TEXT("[SeinNet] LoadReplay: load OK but Play() rejected — check log (likely networked world; replay needs Standalone)."));
		}
	}

	void HandleStopReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] StopReplay: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] StopReplay: USeinNetSubsystem missing.")); return; }
		USeinReplayReader* Reader = Net->GetOrCreateReplayReader();
		if (!Reader || !Reader->IsPlaying())
		{
			Ar.Log(TEXT("[SeinNet] StopReplay: no replay currently playing."));
			return;
		}
		Reader->Stop();
		Ar.Log(TEXT("[SeinNet] StopReplay: halted."));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLoadReplay(
		TEXT("Sein.Net.LoadReplay"),
		TEXT("Sein.Net.LoadReplay <FileNameOrPath> — load + play a .seinreplay file. Bare filenames resolve against Saved/Replays/. Standalone-mode only (close any multiplayer session first)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLoadReplay));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStopReplay(
		TEXT("Sein.Net.StopReplay"),
		TEXT("Halt the currently-playing replay (sim continues from current state)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStopReplay));

	// ============== World snapshot (Phase 4 architecture) ==============

	void HandleDumpSnapshot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] DumpSnapshot: no World.")); return; }
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] DumpSnapshot: USeinWorldSubsystem missing.")); return; }

		FSeinWorldSnapshot Snap;
		WorldSub->CaptureSnapshot(Snap);

		// Stamp the session seed if NetSubsystem has one.
		if (USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>())
		{
			Snap.SessionSeed = Net->GetSessionSeed();
		}

		// Serialize via FObjectAndNameAsStringProxyArchive (same primitive the
		// replay writer uses) so TObjectPtr / FInstancedStruct / soft refs
		// round-trip cleanly.
		TArray<uint8> Buf;
		FMemoryWriter MemWriter(Buf, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadIfFindFails*/ false);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Writer, &Snap, nullptr);

		const FString FileName = Args.Num() > 0
			? Args[0]
			: FString::Printf(TEXT("Snapshot_%s_tick%d.seinsnapshot"),
				*Snap.MapIdentifier.ToString(),
				Snap.CurrentTick);
		const FString DirPath = FPaths::ProjectSavedDir() / TEXT("Snapshots");
		const FString FullPath = FPaths::IsRelative(FileName) ? (DirPath / FileName) : FileName;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree*/ true);

		if (FFileHelper::SaveArrayToFile(Buf, *FullPath))
		{
			Ar.Logf(TEXT("[SeinNet] DumpSnapshot: %d bytes -> %s"), Buf.Num(), *FullPath);
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] DumpSnapshot: write FAILED to %s"), *FullPath);
		}
	}

	void HandleLoadSnapshot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LoadSnapshot: no World.")); return; }
		if (Args.Num() < 1)
		{
			Ar.Log(TEXT("[SeinNet] LoadSnapshot: usage: Sein.Net.LoadSnapshot <FileNameOrPath>"));
			return;
		}
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] LoadSnapshot: USeinWorldSubsystem missing.")); return; }

		// Snapshot LOAD is a single-peer state rewind. Doing this in a live
		// networked session unilaterally rolls back THIS peer's sim tick while
		// every other peer continues forward — the lockstep gate stalls
		// permanently because the local turn cursor goes backward but the
		// network-layer state doesn't (server has already dispatched turns
		// 100..N, local sim now wants turn 67 again, those turns never re-fire).
		//
		// Multi-peer resync (server captures + chunked snapshot RPC + tail
		// catch-up to all peers) is the right path for live-session restore;
		// that's the drop-in/drop-out catch-up RPC infrastructure, not yet
		// built. Until then, refuse with a clear message.
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_Standalone)
		{
			Ar.Logf(TEXT("[SeinNet] LoadSnapshot: refused — world is networked (NetMode=%d). Single-peer snapshot rewind would break lockstep across the other peers. Test snapshot dump/load in a Standalone (single-window) PIE session. Multi-peer snapshot resync is a follow-up phase (drop-in/drop-out catch-up RPC)."),
				(int32)Mode);
			return;
		}

		// Resolve bare filenames against Saved/Snapshots/.
		FString Path = Args[0];
		if (!FPaths::FileExists(Path))
		{
			const FString Resolved = FPaths::ProjectSavedDir() / TEXT("Snapshots") / Path;
			if (FPaths::FileExists(Resolved)) Path = Resolved;
			else if (!Path.EndsWith(TEXT(".seinsnapshot")))
			{
				const FString WithExt = FPaths::ProjectSavedDir() / TEXT("Snapshots") / (Path + TEXT(".seinsnapshot"));
				if (FPaths::FileExists(WithExt)) Path = WithExt;
			}
		}

		TArray<uint8> Buf;
		if (!FFileHelper::LoadFileToArray(Buf, *Path))
		{
			Ar.Logf(TEXT("[SeinNet] LoadSnapshot: failed to read %s"), *Path);
			return;
		}

		FSeinWorldSnapshot Snap;
		FMemoryReader MemReader(Buf, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadIfFindFails*/ true);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Reader, &Snap, nullptr);
		if (Reader.IsError() || Reader.IsCriticalError())
		{
			Ar.Log(TEXT("[SeinNet] LoadSnapshot: deserialization error."));
			return;
		}

		Ar.Logf(TEXT("[SeinNet] LoadSnapshot: header tick=%d  seed=%lld  map=%s  recorded=%s  entities=%d  componentBlobs=%d"),
			Snap.CurrentTick, Snap.SessionSeed, *Snap.MapIdentifier.ToString(),
			*Snap.CapturedAt.ToString(), Snap.Entities.Num(), Snap.ComponentStorageBlobs.Num());

		const bool bOk = WorldSub->RestoreSnapshot(Snap);
		Ar.Logf(TEXT("[SeinNet] LoadSnapshot: restore result = %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdDumpSnapshot(
		TEXT("Sein.Net.DumpSnapshot"),
		TEXT("Sein.Net.DumpSnapshot [FileName] — capture current sim state to Saved/Snapshots/<FileName>. Default filename = Snapshot_<Map>_tick<N>.seinsnapshot."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleDumpSnapshot));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLoadSnapshot(
		TEXT("Sein.Net.LoadSnapshot"),
		TEXT("Sein.Net.LoadSnapshot <FileNameOrPath> — restore sim state from a .seinsnapshot file. NOTE: ability/resolver pool reconstruction is deferred — abilities reset on restore."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLoadSnapshot));

	// ============== Drop-in / drop-out (Phase 4) ==============

	const TCHAR* SlotLifecycleToString(ESeinSlotLifecycle S)
	{
		switch (S)
		{
		case ESeinSlotLifecycle::Connected:    return TEXT("Connected");
		case ESeinSlotLifecycle::Dropped:      return TEXT("Dropped");
		case ESeinSlotLifecycle::AITakeover:   return TEXT("AITakeover");
		case ESeinSlotLifecycle::Reconnecting: return TEXT("Reconnecting");
		default:                               return TEXT("?");
		}
	}

	void HandleSimulateDisconnect(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: no World.")); return; }
		if (Args.Num() < 1) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: usage: Sein.Net.SimulateDisconnect <SlotIndex>")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: SERVER ONLY.")); return; }

		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateDisconnect: USeinNetSubsystem missing.")); return; }

		const int32 SlotInt = FCString::Atoi(*Args[0]);
		if (SlotInt <= 0 || SlotInt > 255) { Ar.Logf(TEXT("[SeinNet] SimulateDisconnect: invalid slot %d"), SlotInt); return; }
		Net->SimulateSlotDisconnect(FSeinPlayerID(static_cast<uint8>(SlotInt)));
		Ar.Logf(TEXT("[SeinNet] SimulateDisconnect: slot %d marked DROPPED."), SlotInt);
	}

	void HandleSimulateReconnect(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: no World.")); return; }
		if (Args.Num() < 1) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: usage: Sein.Net.SimulateReconnect <SlotIndex>")); return; }
		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: SERVER ONLY.")); return; }

		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SimulateReconnect: USeinNetSubsystem missing.")); return; }

		const int32 SlotInt = FCString::Atoi(*Args[0]);
		if (SlotInt <= 0 || SlotInt > 255) { Ar.Logf(TEXT("[SeinNet] SimulateReconnect: invalid slot %d"), SlotInt); return; }
		Net->SimulateSlotReconnect(FSeinPlayerID(static_cast<uint8>(SlotInt)));
		Ar.Logf(TEXT("[SeinNet] SimulateReconnect: slot %d back to CONNECTED."), SlotInt);
	}

	void HandleSlotStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SlotStatus: no World.")); return; }
		USeinNetSubsystem* Net = World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		if (!Net) { Ar.Log(TEXT("[SeinNet] SlotStatus: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Log(TEXT("[SeinNet] SlotStatus: SERVER ONLY (lifecycle is server-side state)."));
			return;
		}

		const TMap<FSeinPlayerID, ESeinSlotLifecycle>& Map = Net->GetSlotLifecycle();
		Ar.Logf(TEXT("[SeinNet] Slot lifecycle (%d slot(s)):"), Map.Num());
		for (const auto& Pair : Map)
		{
			Ar.Logf(TEXT("  slot=%u  status=%s"), Pair.Key.Value, SlotLifecycleToString(Pair.Value));
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateDisconnect(
		TEXT("Sein.Net.SimulateDisconnect"),
		TEXT("SERVER ONLY. Sein.Net.SimulateDisconnect <SlotIndex> — mark a slot as Dropped. Server starts injecting heartbeats so the gate doesn't stall; AI-takeover transition fires after the configured timeout."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateDisconnect));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSimulateReconnect(
		TEXT("Sein.Net.SimulateReconnect"),
		TEXT("SERVER ONLY. Sein.Net.SimulateReconnect <SlotIndex> — mark a slot back to Connected. NOTE: full snapshot+tail catch-up is a follow-up phase — slot resumes from current sim state."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSimulateReconnect));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSlotStatus(
		TEXT("Sein.Net.SlotStatus"),
		TEXT("SERVER ONLY. Dump per-slot lifecycle state (Connected / Dropped / AITakeover / Reconnecting)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSlotStatus));

	// ============== Latency / straggler report (Phase 4 polish) ==============

	void HandleLatencyReport(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] LatencyReport: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] LatencyReport: USeinNetSubsystem missing.")); return; }

		const ENetMode Mode = World->GetNetMode();
		if (Mode != NM_ListenServer && Mode != NM_DedicatedServer)
		{
			Ar.Log(TEXT("[SeinNet] LatencyReport: SERVER ONLY — run on the host."));
			return;
		}

		const int32 Total = Net->GetTurnsCompletedCount();
		const TMap<FSeinPlayerID, int32>& Counts = Net->GetStragglerCounts();
		Ar.Logf(TEXT("[SeinNet] Latency report (TotalTurnsCompleted=%d):"), Total);
		if (Counts.IsEmpty() || Total == 0)
		{
			Ar.Log(TEXT("  No straggle events recorded — every peer is keeping up."));
			return;
		}
		for (const TPair<FSeinPlayerID, int32>& Pair : Counts)
		{
			const float Rate = static_cast<float>(Pair.Value) / static_cast<float>(Total);
			Ar.Logf(TEXT("  slot=%u  late-submit count=%d  rate=%.2f%%  %s"),
				Pair.Key.Value, Pair.Value, Rate * 100.0f,
				Rate > 0.05f ? TEXT("← consider raising InputDelayTurns to absorb this peer's latency") : TEXT(""));
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdLatencyReport(
		TEXT("Sein.Net.LatencyReport"),
		TEXT("SERVER ONLY. Dump per-peer straggle counts so you can see which connection is the slowest. >5% straggle rate suggests bumping InputDelayTurns."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleLatencyReport));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStatus(
		TEXT("Sein.Net.Status"),
		TEXT("Dump SeinARTS networking status: NetMode, active, LocalPlayerID, SessionSeed, slot/relay counts."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStatus));

	void HandleSaveReplay(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] SaveReplay: no World.")); return; }
		UGameInstance* GI = World->GetGameInstance();
		USeinNetSubsystem* Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net) { Ar.Log(TEXT("[SeinNet] SaveReplay: USeinNetSubsystem missing.")); return; }

		USeinReplayWriter* Writer = Net->GetReplayWriter();
		if (!Writer)
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: no ReplayWriter (server-only — run on the host)."));
			return;
		}
		if (!Writer->IsRecording())
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: writer is not recording (StartMatch hasn't been called yet?)."));
			return;
		}

		const int32 BufferedTurns = Writer->GetBufferedTurnCount();
		const FString Path = Writer->FinishRecording();
		if (Path.IsEmpty())
		{
			Ar.Log(TEXT("[SeinNet] SaveReplay: write failed — see log."));
		}
		else
		{
			Ar.Logf(TEXT("[SeinNet] SaveReplay: wrote %d turn(s) -> %s"), BufferedTurns, *Path);
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStartMatch(
		TEXT("Sein.Net.StartMatch"),
		TEXT("SERVER ONLY. Start the lockstep session: fan Client_StartSession to every connected relay so all peers' sims start simultaneously at tick 0 with the gate engaged."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStartMatch));

	void HandleDumpState(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (!World) { Ar.Log(TEXT("[SeinNet] DumpState: no World.")); return; }
		USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
		USeinNetSubsystem* Net = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!WorldSub) { Ar.Log(TEXT("[SeinNet] DumpState: no USeinWorldSubsystem.")); return; }

		const int32 Tick = WorldSub->GetCurrentTick();
		const int32 Hash = WorldSub->ComputeStateHash();
		const int32 ActiveEntities = WorldSub->GetEntityPool().GetActiveCount();
		const int64 Seed = Net ? Net->GetSessionSeed() : 0;
		const FSeinPlayerID LocalSlot = Net ? Net->GetLocalPlayerID() : FSeinPlayerID::Neutral();

		Ar.Logf(TEXT("[SeinNet] DumpState  Tick=%d  StateHash=0x%08x  ActiveEntities=%d  Seed=%lld  LocalSlot=%u  NetMode=%d"),
			Tick, (uint32)Hash, ActiveEntities, Seed, LocalSlot.Value, (int32)World->GetNetMode());

		// Server-only: dump the slot↔relay binding so cross-run comparisons can
		// confirm Player 1 = SeinPlayerController_0, etc.
		if (Net && (World->GetNetMode() == NM_ListenServer || World->GetNetMode() == NM_DedicatedServer))
		{
			Ar.Logf(TEXT("[SeinNet] Slot bindings (server view, %d slot(s)):"), Net->GetActiveSlotCount());
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				if (ASeinNetRelay* R = Wp.Get())
				{
					Ar.Logf(TEXT("  slot=%u  relay=%s  owner=%s"),
						R->AssignedPlayerID.Value, *GetNameSafe(R), *GetNameSafe(R->GetOwner()));
				}
			}
		}
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdSaveReplay(
		TEXT("Sein.Net.SaveReplay"),
		TEXT("SERVER ONLY. Manually flush the in-memory replay buffer to Saved/Replays/. Otherwise auto-flushes on session teardown (PIE Stop)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleSaveReplay));

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdDumpState(
		TEXT("Sein.Net.DumpState"),
		TEXT("Dump current sim state: tick, state hash, entity count, session seed, local slot. Run on every window in turn — same Tick should produce same StateHash on every machine if lockstep is healthy."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleDumpState));
}
