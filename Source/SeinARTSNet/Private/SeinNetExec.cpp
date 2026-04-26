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
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Types/Vector.h"

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
		Net->SubmitLocalCommand(TurnId, Cmd);
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

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCmdStatus(
		TEXT("Sein.Net.Status"),
		TEXT("Dump SeinARTS networking status: NetMode, active, LocalPlayerID, SessionSeed, slot/relay counts."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleStatus));
}
