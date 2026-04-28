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
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Vector.h"
#include "GameFramework/PlayerController.h"

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

		Ar.Logf(TEXT("[SeinNet] StartMatch: triggering lockstep session start across %d relay(s)."), Net->GetRelays().Num());
		Net->StartLockstepSession();
	}

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
