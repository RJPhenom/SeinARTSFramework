/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.cpp
 */

#include "SeinNetSubsystem.h"
#include "SeinARTSNet.h"
#include "SeinNetRelay.h"
#include "SeinReplayWriter.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"

void USeinNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &USeinNetSubsystem::OnPostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &USeinNetSubsystem::OnLogout);

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem initialized."));
}

void USeinNetSubsystem::Deinitialize()
{
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);

	// Flush the replay log to disk on session teardown. PIE Stop = end of
	// match for our purposes, so we always write a file. If the writer isn't
	// recording (e.g. session never started), FinishRecording is a no-op.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		const FString WrittenPath = ReplayWriter->FinishRecording();
		if (!WrittenPath.IsEmpty())
		{
			UE_LOG(LogSeinNet, Log, TEXT("Deinitialize: replay flushed -> %s"), *WrittenPath);
		}
	}
	ReplayWriter = nullptr;

	// Tear down WorldSubsystem-side hooks. The world may already be gone
	// (Deinitialize ordering during PIE shutdown), so be defensive.
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->TurnReadyResolver.Unbind();
			WorldSub->TurnConsumeNotifier.Unbind();
			if (TickCompletedHandle.IsValid())
			{
				WorldSub->OnSimTickCompleted.Remove(TickCompletedHandle);
			}
		}
	}
	TickCompletedHandle.Reset();

	Relays.Reset();
	LocalRelay.Reset();
	RelayToSlot.Reset();
	ServerTurnBuffer.Reset();
	CompletedTurns.Reset();
	ReceivedTurns.Reset();
	PendingOutgoingCommands.Reset();
	LastSubmittedTurn = -1;
	LocalPlayerID = FSeinPlayerID::Neutral();
	SessionSeed = 0;

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem deinitialized."));
	Super::Deinitialize();
}

int32 USeinNetSubsystem::GetTicksPerTurn() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || Settings->TurnRate <= 0) return 1;
	return FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate);
}

int32 USeinNetSubsystem::GetInputDelayTurns() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return (Settings && Settings->InputDelayTurns > 0) ? Settings->InputDelayTurns : 3;
}

bool USeinNetSubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer;
}

bool USeinNetSubsystem::IsNetworkingActive() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetNetMode() == NM_Standalone) return false;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bNetworkingEnabled;
}

void USeinNetSubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer) return;
	if (!IsServer()) return;

	UWorld* World = GameMode->GetWorld();
	if (!World) return;
	if (World->GetGameInstance() != GetGameInstance())
	{
		// Some other GI's GameMode (e.g. a sub-PIE world) — ignore.
		return;
	}

	// Phase 3a: relay spawn moved into ServerSpawnRelayForController, which
	// is invoked by ASeinGameMode::HandleStartingNewPlayer AFTER it binds
	// the controller's slot. This eliminates the dual-source-of-truth bug
	// where the old auto-spawn-here path independently sequenced slots
	// (NextSlotToAssign++) while GameMode independently picked from match
	// settings — they could disagree if connection order ≠ slot order.
	//
	// We still ensure the session seed is locked at first PostLogin so
	// that whenever the relay spawns, the seed is already stable.
	EnsureSessionSeed();

	UE_LOG(LogSeinNet, Verbose, TEXT("OnPostLogin: PC=%s noted (relay spawn deferred to GameMode binding flow)."),
		*GetNameSafe(NewPlayer));
}

void USeinNetSubsystem::ServerSpawnRelayForController(APlayerController* PC, FSeinPlayerID Slot)
{
	if (!IsServer()) return;
	if (!PC)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: null PC."));
		return;
	}
	if (!Slot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: invalid slot for PC %s — no relay spawned (legacy auto-assign path?)."),
			*GetNameSafe(PC));
		return;
	}

	UWorld* World = PC->GetWorld();
	if (!World) return;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->bNetworkingEnabled)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("ServerSpawnRelayForController: networking disabled — skipping."));
		return;
	}

	// Idempotence: if this PC already has a relay (re-bind / seamless travel),
	// re-stamp it instead of spawning a duplicate. Replicates the new slot
	// value to the owning client via OnRep_AssignedPlayerID.
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* R = Wp.Get())
		{
			if (R->GetOwner() == PC)
			{
				R->AssignedPlayerID = Slot;
				R->SessionSeed = SessionSeed;
				RelayToSlot.Add(R, Slot);
				UE_LOG(LogSeinNet, Log, TEXT("ServerSpawnRelayForController: existing relay %s re-stamped slot=%u for %s"),
					*GetNameSafe(R), Slot.Value, *GetNameSafe(PC));
				return;
			}
		}
	}

	UClass* RelayClass = Settings->RelayActorClass.TryLoadClass<ASeinNetRelay>();
	if (!RelayClass) RelayClass = ASeinNetRelay::StaticClass();

	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// Deferred spawn so we can stamp AssignedPlayerID + SessionSeed BEFORE
	// BeginPlay runs (and before initial replication ships). On a Listen
	// Server, BeginPlay fires synchronously inside SpawnActor — if we stamp
	// after, RegisterRelay sees a zero slot and the host's LocalPlayerID
	// never latches (server-side has no OnRep callback).
	Params.bDeferConstruction = true;

	ASeinNetRelay* Relay = World->SpawnActor<ASeinNetRelay>(RelayClass, FTransform::Identity, Params);
	if (!Relay)
	{
		UE_LOG(LogSeinNet, Error, TEXT("ServerSpawnRelayForController: failed to spawn relay for %s slot=%u"),
			*GetNameSafe(PC), Slot.Value);
		return;
	}

	EnsureSessionSeed();
	Relay->AssignedPlayerID = Slot;
	Relay->SessionSeed = SessionSeed;
	RelayToSlot.Add(Relay, Slot);

	Relay->FinishSpawning(FTransform::Identity);

	UE_LOG(LogSeinNet, Log, TEXT("ServerSpawnRelayForController: spawned %s for %s  slot=%u  seed=%lld"),
		*GetNameSafe(Relay), *GetNameSafe(PC), Slot.Value, SessionSeed);
}

void USeinNetSubsystem::EnsureSessionSeed()
{
	if (SessionSeed != 0) return;

	// Debug override: when settings.DebugFixedSessionSeed is nonzero, lock the
	// seed to that exact value so successive PIE Plays produce bit-identical
	// sim runs (any PRNG-driven variance disappears, leaving only true
	// determinism bugs visible). Production builds leave it at 0 and the
	// random path runs.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && Settings->DebugFixedSessionSeed != 0)
	{
		SessionSeed = Settings->DebugFixedSessionSeed;
		UE_LOG(LogSeinNet, Warning, TEXT("EnsureSessionSeed: using DebugFixedSessionSeed=%lld (DEBUG; clear in production)."), SessionSeed);
		return;
	}

	// Server-generated random seed combining wall clock + cycle counter. The
	// lockstep invariant only requires that ALL clients agree on the seed
	// before tick 0 — how it was generated is private to the host.
	const int64 NowCycles = (int64)FPlatformTime::Cycles64();
	const int64 NowTicks = FDateTime::UtcNow().GetTicks();
	SessionSeed = (NowCycles ^ NowTicks);
	if (SessionSeed == 0) SessionSeed = 1; // 0 reads as "uninitialized" elsewhere.
	UE_LOG(LogSeinNet, Log, TEXT("EnsureSessionSeed: generated SessionSeed=%lld"), SessionSeed);
}

void USeinNetSubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!Exiting || !IsServer()) return;

	// Snapshot first — AActor::Destroy() synchronously fires EndPlay, which
	// calls back into UnregisterRelay() and mutates the Relays array. Iterating
	// + mutating in the same loop walks off the end (crash: index 0 into an
	// array of size 0). Collect targets, THEN destroy.
	TArray<ASeinNetRelay*> ToDestroy;
	ToDestroy.Reserve(Relays.Num());
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Relay = Wp.Get())
		{
			if (Relay->GetOwner() == Exiting)
			{
				ToDestroy.Add(Relay);
			}
		}
	}
	for (ASeinNetRelay* Relay : ToDestroy)
	{
		UE_LOG(LogSeinNet, Log, TEXT("OnLogout: destroying relay %s (owner %s leaving)"),
			*GetNameSafe(Relay), *GetNameSafe(Exiting));
		// Drop slot mapping before Destroy → EndPlay → UnregisterRelay so the
		// completeness gate doesn't keep waiting for a player who left.
		RelayToSlot.Remove(Relay);
		Relay->Destroy();
	}

	// Recheck every open turn — a dropped player may have been the missing
	// piece, in which case those turns should fan out NOW with whatever's
	// already in the buffer instead of stalling forever. Snapshot keys
	// because ServerCheckTurnComplete mutates ServerTurnBuffer on completion.
	if (!ToDestroy.IsEmpty() && !ServerTurnBuffer.IsEmpty())
	{
		TArray<int32> OpenTurns;
		ServerTurnBuffer.GetKeys(OpenTurns);
		for (int32 TurnId : OpenTurns)
		{
			ServerCheckTurnComplete(TurnId);
		}
	}
}

void USeinNetSubsystem::RegisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	Relays.AddUnique(Relay);

	// On the client side (and on the host's own PC), latch the local relay.
	// Identification: the relay's owner is a PlayerController whose IsLocalController() is true.
	if (APlayerController* PC = Cast<APlayerController>(Relay->GetOwner()))
	{
		if (PC->IsLocalController())
		{
			LocalRelay = Relay;
			UE_LOG(LogSeinNet, Log, TEXT("RegisterRelay: latched LOCAL relay %s (PC=%s)"),
				*GetNameSafe(Relay), *GetNameSafe(PC));

			// On a Listen Server, AssignedPlayerID + SessionSeed were stamped
			// before FinishSpawning (deferred-spawn path in OnPostLogin), so
			// they're valid here. OnRep_AssignedPlayerID does NOT fire on the
			// server, so without this eager latch the host's LocalPlayerID
			// would stay zero. On a remote client, AssignedPlayerID is still
			// neutral here (initial rep hasn't arrived yet) — OnRep latches it.
			if (Relay->AssignedPlayerID.IsValid())
			{
				NotifyLocalSlotAssigned(Relay, Relay->AssignedPlayerID, Relay->SessionSeed);
			}
		}
	}
}

void USeinNetSubsystem::UnregisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	Relays.RemoveAllSwap([Relay](const TWeakObjectPtr<ASeinNetRelay>& Wp) { return Wp.Get() == Relay; });
	RelayToSlot.Remove(Relay);
	if (LocalRelay.Get() == Relay)
	{
		LocalRelay.Reset();
		// Note: keep LocalPlayerID + SessionSeed cached. A relay swap (e.g.
		// seamless travel) would re-stamp them; clearing here would briefly
		// flash a "no slot" state for any UI binding to GetLocalPlayerID.
	}
}

void USeinNetSubsystem::NotifyLocalSlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot, int64 Seed)
{
	if (!Relay) return;
	if (LocalRelay.Get() != Relay)
	{
		// OnRep can fire before RegisterRelay(BeginPlay) has run on the client.
		// Latch LocalRelay here too — RegisterRelay is idempotent on the
		// AddUnique path.
		LocalRelay = Relay;
	}
	LocalPlayerID = Slot;
	SessionSeed = Seed;
	UE_LOG(LogSeinNet, Log, TEXT("NotifyLocalSlotAssigned: LocalPlayerID=%u  SessionSeed=%lld"),
		LocalPlayerID.Value, SessionSeed);

	// Wire the lockstep gate immediately so when the sim DOES start (via
	// StartLocalSession from server's Client_StartSession), it ticks gated
	// from frame one. We do NOT start the sim here — sim start is gated on
	// the explicit StartLockstepSession trigger so all peers come up at
	// tick 0 simultaneously. Without that, the host (which connects first)
	// would tick alone, dispatch turns with `1/1 slots`, mark them as
	// `CompletedTurns`, and reject all subsequent client submissions as
	// "late" — soft-locking every peer.
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return;

	// Bind gate hooks. WeakLambda so a Deinitialize race doesn't strand the
	// delegate holding `this`.
	TWeakObjectPtr<USeinNetSubsystem> WeakSelf(this);
	WorldSub->TurnReadyResolver.BindLambda([WeakSelf](int32 Turn)
	{
		USeinNetSubsystem* Self = WeakSelf.Get();
		return Self ? Self->ResolveTurnReady(Turn) : true; // unbinding self = no gate
	});
	WorldSub->TurnConsumeNotifier.BindLambda([WeakSelf](int32 Turn)
	{
		if (USeinNetSubsystem* Self = WeakSelf.Get())
		{
			Self->ConsumeTurn(Turn);
		}
	});

	if (!TickCompletedHandle.IsValid())
	{
		TickCompletedHandle = WorldSub->OnSimTickCompleted.AddUObject(this, &USeinNetSubsystem::OnSimTickCompleted);
	}

	UE_LOG(LogSeinNet, Log, TEXT("NotifyLocalSlotAssigned: gate hooks bound (sim deferred until Client_StartSession)  TicksPerTurn=%d  InputDelay=%d turns."),
		GetTicksPerTurn(), GetInputDelayTurns());
}

void USeinNetSubsystem::StartLocalSession()
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("StartLocalSession: no USeinWorldSubsystem on World."));
		return;
	}
	if (WorldSub->IsSimulationRunning())
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("StartLocalSession: sim already running — no-op."));
		return;
	}
	UE_LOG(LogSeinNet, Log, TEXT("StartLocalSession: starting sim at tick 0 with gate engaged."));
	WorldSub->StartSimulation();

	// Pre-fire the first non-grace heartbeat (turn = InputDelayTurns). Without
	// this, configs where `InputDelayTurns × TicksPerTurn < 2` (only TPT=1 +
	// ID=1 today, but any future config that lands here) dead-drop: there are
	// zero grace ticks for OnSimTickCompleted to fire a heartbeat in before
	// the sim hits the first gated boundary. Pre-firing seeds the gate so
	// every connected peer can fan out turn `InputDelayTurns` and the sim
	// makes progress.
	//
	// Benign for all other configs — the catch-up loop in OnSimTickCompleted
	// sees LastSubmittedTurn already at InputDelayTurns and skips redundant
	// re-submission for that turn.
	if (LocalPlayerID.IsValid() && IsNetworkingActive())
	{
		const int32 InputDelay = GetInputDelayTurns();
		if (LastSubmittedTurn < InputDelay)
		{
			UE_LOG(LogSeinNet, Verbose, TEXT("StartLocalSession: pre-firing heartbeat for turn %d (gate seed)."), InputDelay);
			const TArray<FSeinCommand> Empty;
			SubmitLocalCommandsAtTurn(InputDelay, Empty);
			LastSubmittedTurn = InputDelay;
		}
	}
}

void USeinNetSubsystem::StartLockstepSession()
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("StartLockstepSession: callable only on server — ignored."));
		return;
	}

	UE_LOG(LogSeinNet, Log, TEXT("StartLockstepSession: firing Client_StartSession on %d relay(s) and starting local sim."),
		Relays.Num());

	// Open the replay writer BEFORE fanning out so RecordTurn captures every
	// turn from the very first one. Header uses the framework's existing
	// FSeinReplayHeader (SeinReplayHeader.h) — RandomSeed = SessionSeed so
	// the deterministic PRNG seed is captured for replay re-runs.
	if (!ReplayWriter)
	{
		ReplayWriter = NewObject<USeinReplayWriter>(this);
	}
	if (ReplayWriter && !ReplayWriter->IsRecording())
	{
		FSeinReplayHeader Header;
		Header.FrameworkVersion = TEXT("0.1.0");
		Header.GameVersion      = TEXT("unset");
		Header.MapIdentifier    = GetWorld() ? FName(*GetWorld()->GetMapName()) : NAME_None;
		Header.RandomSeed       = SessionSeed;
		// SettingsSnapshot + Players left default for now — Phase 4b can
		// populate from match-flow once lobby is in. Reader compares against
		// current sim state and warns on mismatch.
		Header.StartTick        = 0;
		Header.EndTick          = 0; // updated on FinishRecording if needed
		Header.RecordedAt       = FDateTime::UtcNow();
		ReplayWriter->StartRecording(Header);
	}

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Target = Wp.Get())
		{
			Target->Client_StartSession();
		}
	}

	// Server's own sim. Local-call so the host doesn't depend on receiving
	// its own Client RPC (which it would, but explicit is safer + cheaper).
	StartLocalSession();
}

bool USeinNetSubsystem::ResolveTurnReady(int32 Turn)
{
	// Grace period: the first InputDelayTurns turns can never have submissions
	// (no input could have been issued before sim started), so unconditionally
	// pass them. This matches the heartbeat schedule below — first heartbeat
	// is for turn `0 + InputDelay`, fired after sim's first turn worth of
	// ticks complete, so turn `InputDelay` is the first gated turn.
	if (Turn < GetInputDelayTurns()) return true;

	const bool bReady = ReceivedTurns.Contains(Turn);
	if (!bReady)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("ResolveTurnReady: turn %d NOT ready — sim stalls."), Turn);
	}
	return bReady;
}

void USeinNetSubsystem::ConsumeTurn(int32 Turn)
{
	TArray<FSeinCommand> Drained;
	if (!ReceivedTurns.RemoveAndCopyValue(Turn, Drained))
	{
		// Grace turn (no entry stored) or already drained — nothing to do.
		return;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return;

	for (const FSeinCommand& Cmd : Drained)
	{
		WorldSub->EnqueueCommand(Cmd);
	}
	UE_LOG(LogSeinNet, Verbose, TEXT("ConsumeTurn: drained turn %d (%d cmd(s)) into PendingCommands."),
		Turn, Drained.Num());
}

void USeinNetSubsystem::OnSimTickCompleted(int32 CompletedTick)
{
	if (!IsNetworkingActive()) return;
	if (!LocalPlayerID.IsValid()) return;

	const int32 TicksPerTurn = GetTicksPerTurn();
	const int32 NextTick = CompletedTick + 1;

	// Fire only on turn boundaries — i.e., the tick we just completed was the
	// last tick of a turn (so the next tick starts a new turn).
	if (NextTick % TicksPerTurn != 0) return;

	// Compute outgoing turn: commands accumulated during the turn we just
	// finished apply at `current_turn + InputDelay`. Idempotent guard against
	// multiple OnSimTickCompleted fires within the same boundary.
	const int32 JustFinishedTurn = CompletedTick / TicksPerTurn;
	const int32 InputDelay = GetInputDelayTurns();
	const int32 OutgoingTurn = JustFinishedTurn + InputDelay;
	if (OutgoingTurn <= LastSubmittedTurn) return;

	// Catch-up loop: send heartbeats for every non-grace turn from
	// (LastSubmittedTurn+1, but not before the first non-grace turn = InputDelay)
	// up through OutgoingTurn. Without this, certain (TicksPerTurn, InputDelay)
	// combinations skip past the first non-grace turn entirely.
	//
	//   Example: TicksPerTurn=1, InputDelay=2. The first tick we process is
	//   tick 1 (tick 0 is initial state, never broadcast). OnSimTickCompleted(1)
	//   fires, JustFinishedTurn=1, OutgoingTurn=3 — so without catch-up we'd
	//   submit for turn 3 and skip turn 2 entirely. Sim stalls on turn 2 forever.
	//
	// User commands attach to the LATEST outgoing turn only; earlier turns are
	// pure heartbeats. This preserves input-delay semantics (input issued during
	// turn N applies at turn N + InputDelay) while keeping the gate completable
	// for every non-grace turn from session start onward.
	const int32 FirstOutgoing = FMath::Max(LastSubmittedTurn + 1, InputDelay);

	TArray<FSeinCommand> ToSend = MoveTemp(PendingOutgoingCommands);
	PendingOutgoingCommands.Reset();
	const TArray<FSeinCommand> Empty;

	for (int32 T = FirstOutgoing; T <= OutgoingTurn; ++T)
	{
		const TArray<FSeinCommand>& Payload = (T == OutgoingTurn) ? ToSend : Empty;
		UE_LOG(LogSeinNet, Verbose, TEXT("OnSimTickCompleted(%d): turn-boundary flush  outgoing=%d  cmds=%d (heartbeat=%d)"),
			CompletedTick, T, Payload.Num(), Payload.IsEmpty() ? 1 : 0);
		SubmitLocalCommandsAtTurn(T, Payload);
	}
	LastSubmittedTurn = OutgoingTurn;
}

void USeinNetSubsystem::SubmitLocalCommand(const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	SubmitLocalCommands(Single);
}

void USeinNetSubsystem::SubmitLocalCommands(const TArray<FSeinCommand>& Commands)
{
	if (Commands.Num() == 0) return;

	// Standalone path: bypass everything and drop straight into the local
	// sim's command buffer. Single-player is zero-network-overhead.
	if (!IsNetworkingActive())
	{
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommands [Standalone]: no USeinWorldSubsystem — dropping %d cmd(s)."), Commands.Num());
			return;
		}
		for (const FSeinCommand& Cmd : Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		return;
	}

	// Networked path (Phase 2b): accumulate in PendingOutgoingCommands. The
	// OnSimTickCompleted handler flushes them at the next turn boundary,
	// stamped with the correct outgoing TurnId. This guarantees a single
	// per-turn submission per client (commands + heartbeat in one packet),
	// which is what the server's gate needs to complete deterministically.
	PendingOutgoingCommands.Append(Commands);
	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommands: buffered %d cmd(s) (pending=%d) for next turn flush."),
		Commands.Num(), PendingOutgoingCommands.Num());
}

void USeinNetSubsystem::SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	SubmitLocalCommandsAtTurn(TurnId, Single);
}

void USeinNetSubsystem::SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	// NOTE: empty `Commands` is intentionally allowed — the heartbeat path
	// relies on a per-turn submission even when no input was issued, so the
	// server's gate can complete for that turn. Skipping empties would stall
	// every peer.

	// Standalone / networking-disabled path: skip the relay entirely and
	// drop straight into the world subsystem's command buffer. Single-player
	// is zero-network-overhead; the lockstep wire is purely opt-in.
	if (!IsNetworkingActive())
	{
		if (Commands.Num() == 0) return; // no heartbeat needed in Standalone
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommandsAtTurn: Standalone but no USeinWorldSubsystem — dropping %d cmd(s)."), Commands.Num());
			return;
		}
		UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn [Standalone]: enqueuing %d cmd(s) directly to WorldSubsystem."), Commands.Num());
		for (const FSeinCommand& Cmd : Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		return;
	}

	ASeinNetRelay* Relay = LocalRelay.Get();
	if (!Relay)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommandsAtTurn: no LocalRelay yet (replication pending?) — dropping %d cmd(s)."), Commands.Num());
		return;
	}

	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn: TurnId=%d  Count=%d  Slot=%u  Relay=%s%s"),
		TurnId, Commands.Num(), LocalPlayerID.Value, *GetNameSafe(Relay),
		Commands.IsEmpty() ? TEXT("  [HEARTBEAT]") : TEXT(""));
	Relay->Server_SubmitCommands(TurnId, Commands);
}

void USeinNetSubsystem::ServerHandleSubmission(ASeinNetRelay* SourceRelay, int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (!IsServer() || !SourceRelay) return;

	const FSeinPlayerID* SlotPtr = RelayToSlot.Find(SourceRelay);
	if (!SlotPtr || !SlotPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Server] Submission from unmapped relay %s — rejecting %d cmd(s)."),
			*GetNameSafe(SourceRelay), Commands.Num());
		return;
	}
	const FSeinPlayerID Slot = *SlotPtr;

	if (CompletedTurns.Contains(TurnId))
	{
		// Late submission for an already-dispatched turn. Lockstep can't
		// retroactively splice these in — the turn already shipped. Drop +
		// log; Phase 4 adaptive-input-delay will preempt this by raising
		// the delay before stragglers happen.
		UE_LOG(LogSeinNet, Warning, TEXT("[Server] Late submission slot=%u  TurnId=%d (already dispatched) — dropped."),
			Slot.Value, TurnId);
		return;
	}

	// Stamp authoritative sender on each command. Caller's PlayerID is
	// untrusted — server overrides.
	TArray<FSeinCommand> Stamped = Commands;
	for (FSeinCommand& Cmd : Stamped)
	{
		Cmd.PlayerID = Slot;
		Cmd.Tick = TurnId * GetTicksPerTurn(); // first sim tick where this turn applies
	}

	TMap<FSeinPlayerID, TArray<FSeinCommand>>& BufferForTurn = ServerTurnBuffer.FindOrAdd(TurnId);
	BufferForTurn.Add(Slot, MoveTemp(Stamped));

	UE_LOG(LogSeinNet, Verbose, TEXT("[Server] Buffered submission slot=%u  TurnId=%d  Count=%d  (have %d/%d slots)"),
		Slot.Value, TurnId, Commands.Num(), BufferForTurn.Num(), GetActiveSlotCount());

	ServerCheckTurnComplete(TurnId);
}

void USeinNetSubsystem::ServerCheckTurnComplete(int32 TurnId)
{
	const TMap<FSeinPlayerID, TArray<FSeinCommand>>* BufferForTurn = ServerTurnBuffer.Find(TurnId);
	if (!BufferForTurn) return;

	const int32 ActiveSlots = GetActiveSlotCount();
	if (BufferForTurn->Num() < ActiveSlots) return;
	// Edge case: someone disconnected after submitting; ActiveSlots dropped
	// below buffer size. Treat the turn as complete.

	// Assemble in deterministic order: sort by slot ID. Lockstep needs
	// every client to apply commands in the same sequence; the buffer's
	// TMap iteration order is not stable across machines.
	TArray<FSeinPlayerID> SortedSlots;
	BufferForTurn->GetKeys(SortedSlots);
	SortedSlots.Sort([](const FSeinPlayerID& A, const FSeinPlayerID& B) { return A.Value < B.Value; });

	TArray<FSeinCommand> Assembled;
	for (const FSeinPlayerID& Slot : SortedSlots)
	{
		const TArray<FSeinCommand>& Sub = BufferForTurn->FindChecked(Slot);
		Assembled.Append(Sub);
	}

	UE_LOG(LogSeinNet, Log, TEXT("[Server] Turn complete: TurnId=%d  Slots=%d  TotalCmds=%d — fanning to %d relays."),
		TurnId, SortedSlots.Num(), Assembled.Num(), Relays.Num());

	// Capture the canonical assembled turn into the replay log BEFORE fan-out.
	// Recording at the assembly step (rather than per-client receive) gives us
	// one authoritative copy free of duplicates / ordering ambiguity.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		ReplayWriter->RecordTurn(TurnId, Assembled);
	}

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Target = Wp.Get())
		{
			Target->Client_ReceiveTurn(TurnId, Assembled);
		}
	}

	ServerTurnBuffer.Remove(TurnId);
	CompletedTurns.Add(TurnId);
}

void USeinNetSubsystem::ClientHandleTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	UE_LOG(LogSeinNet, Log, TEXT("[Client] Receive turn: TurnId=%d Count=%d  (buffered for turn-boundary drain)"),
		TurnId, Commands.Num());

	// Phase 2b: store the assembled turn keyed by TurnId. The sim's gate
	// (ResolveTurnReady → ConsumeTurn) drains this map at the matching
	// turn boundary, guaranteeing every client applies turn N's commands
	// at the same sim tick (= N * TicksPerTurn). Empty turns still get an
	// entry so the gate sees them as "ready" instead of stalling.
	ReceivedTurns.Add(TurnId, Commands);

	OnTurnReceived.Broadcast(TurnId, Commands);
}
