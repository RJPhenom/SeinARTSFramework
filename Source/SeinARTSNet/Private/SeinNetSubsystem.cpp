/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.cpp
 */

#include "SeinNetSubsystem.h"
#include "SeinARTSNet.h"
#include "SeinNetRelay.h"
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

	Relays.Reset();
	LocalRelay.Reset();
	RelayToSlot.Reset();
	ServerTurnBuffer.Reset();
	CompletedTurns.Reset();
	NextSlotToAssign = 1;
	LocalPlayerID = FSeinPlayerID::Neutral();
	SessionSeed = 0;

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem deinitialized."));
	Super::Deinitialize();
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

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->bNetworkingEnabled)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("OnPostLogin: networking disabled in settings — skipping relay spawn."));
		return;
	}

	// Resolve relay class via the soft-class setting (defaults to ASeinNetRelay).
	UClass* RelayClass = Settings->RelayActorClass.TryLoadClass<ASeinNetRelay>();
	if (!RelayClass)
	{
		RelayClass = ASeinNetRelay::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.Owner = NewPlayer;
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
		UE_LOG(LogSeinNet, Error, TEXT("OnPostLogin: failed to spawn relay for PC %s"), *GetNameSafe(NewPlayer));
		return;
	}

	EnsureSessionSeed();
	const FSeinPlayerID Slot = ServerAssignNextSlot(Relay);

	// FinishSpawning runs BeginPlay (which calls RegisterRelay; that path now
	// sees a valid AssignedPlayerID and latches LocalPlayerID on the host) and
	// kicks initial replication (so remote clients see the stamped values in
	// the first replication burst, not a delta after the fact).
	Relay->FinishSpawning(FTransform::Identity);

	UE_LOG(LogSeinNet, Log, TEXT("OnPostLogin: spawned %s for %s  slot=%u  seed=%lld"),
		*GetNameSafe(Relay), *GetNameSafe(NewPlayer), Slot.Value, SessionSeed);
}

void USeinNetSubsystem::EnsureSessionSeed()
{
	if (SessionSeed != 0) return;

	// Phase 1: server-generated random seed combining wall clock + PIE PID-ish
	// entropy. Phase 2 lobby flow will let the host pick (or carry it from
	// match settings) so deterministic re-runs are possible. The lockstep
	// invariant only requires that ALL clients agree on the seed before tick
	// 0 — how it was generated is private to the host.
	const int64 NowCycles = (int64)FPlatformTime::Cycles64();
	const int64 NowTicks = FDateTime::UtcNow().GetTicks();
	SessionSeed = (NowCycles ^ NowTicks);
	if (SessionSeed == 0) SessionSeed = 1; // 0 reads as "uninitialized" elsewhere.
	UE_LOG(LogSeinNet, Log, TEXT("EnsureSessionSeed: generated SessionSeed=%lld"), SessionSeed);
}

FSeinPlayerID USeinNetSubsystem::ServerAssignNextSlot(ASeinNetRelay* Relay)
{
	if (!Relay) return FSeinPlayerID::Neutral();

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const uint8 MaxSlots = (Settings && Settings->MaxPlayers > 0) ? (uint8)FMath::Min(Settings->MaxPlayers, 255) : 8;
	if (NextSlotToAssign > MaxSlots)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerAssignNextSlot: MaxPlayers=%u exhausted; rejecting relay %s"),
			MaxSlots, *GetNameSafe(Relay));
		return FSeinPlayerID::Neutral();
	}

	const FSeinPlayerID Slot(NextSlotToAssign++);
	Relay->AssignedPlayerID = Slot;
	Relay->SessionSeed = SessionSeed;
	RelayToSlot.Add(Relay, Slot);
	return Slot;
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
}

void USeinNetSubsystem::SubmitLocalCommand(int32 TurnId, const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	SubmitLocalCommands(TurnId, Single);
}

void USeinNetSubsystem::SubmitLocalCommands(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (Commands.Num() == 0) return;

	// Standalone / networking-disabled path: skip the relay entirely and
	// drop straight into the world subsystem's command buffer. Single-player
	// is zero-network-overhead; the lockstep wire is purely opt-in.
	if (!IsNetworkingActive())
	{
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommands: Standalone but no USeinWorldSubsystem — dropping %d cmd(s)."), Commands.Num());
			return;
		}
		UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommands [Standalone]: enqueuing %d cmd(s) directly to WorldSubsystem."), Commands.Num());
		for (const FSeinCommand& Cmd : Commands)
		{
			WorldSub->EnqueueCommand(Cmd);
		}
		return;
	}

	ASeinNetRelay* Relay = LocalRelay.Get();
	if (!Relay)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommands: no LocalRelay yet (replication pending?) — dropping %d cmd(s)."), Commands.Num());
		return;
	}

	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommands: TurnId=%d  Count=%d  Slot=%u  Relay=%s"),
		TurnId, Commands.Num(), LocalPlayerID.Value, *GetNameSafe(Relay));
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
		// log; Phase 4 adaptive-input-delay will preempt this by raising the
		// delay before stragglers happen.
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
		Cmd.Tick = TurnId; // Phase 1: TurnId == Tick. Phase 2 derives true sim tick from turn boundary.
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
	UE_LOG(LogSeinNet, Log, TEXT("[Client] Apply turn: TurnId=%d Count=%d  (sim integration lands Phase 2)"),
		TurnId, Commands.Num());

	OnTurnReceived.Broadcast(TurnId, Commands);

	// Phase 2: hand `Commands` off to USeinWorldSubsystem's pending turn buffer
	// keyed by TurnId, draining when the sim crosses the matching turn boundary.
}
