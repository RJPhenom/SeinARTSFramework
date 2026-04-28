/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.h
 * @brief   Game-instance subsystem orchestrating the lockstep network layer.
 *
 * Responsibilities (Phase 0):
 *   - On the server, spawn an ASeinNetRelay per APlayerController as it joins
 *     (FGameModeEvents::GameModePostLoginEvent), destroy on logout. Owner =
 *     the PC, so the client legitimately owns its relay for ServerRPC.
 *   - Track all server-side relays + the one client-side local relay.
 *   - Provide SubmitLocalCommand() as the single client entry point.
 *   - Phase 0 server aggregation is a passthrough: incoming submission is
 *     immediately fanned back out to every relay's ClientRPC. Phase 2
 *     replaces this with real per-turn buffering.
 *
 * Networking is gated on:
 *     USeinARTSCoreSettings::bNetworkingEnabled  AND
 *     World->GetNetMode() != NM_Standalone
 * If either is false the subsystem stays alive but every entry point is a
 * no-op — single-player builds are zero-overhead.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "SeinNetSubsystem.generated.h"

class ASeinNetRelay;
class APlayerController;
class AGameModeBase;
class AController;
class USeinReplayWriter;

UCLASS()
class SEINARTSNET_API USeinNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Player-input entry point. Stamps an automatically-computed TurnId
	 *  derived from the current sim tick + InputDelayTurns. Caller doesn't
	 *  need to think about turns — just hand it the command.
	 *
	 *  In Standalone (or when networking is disabled in settings) the command
	 *  bypasses the relay and goes directly into USeinWorldSubsystem's
	 *  command buffer — single-player is zero-overhead. In a networked
	 *  session, forwards to the local relay's Server_SubmitCommands; the
	 *  server's AssignedPlayerID overrides the command's PlayerID before
	 *  fan-out, so callers don't need to fill it correctly (server will). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SubmitLocalCommand(const FSeinCommand& Command);

	/** Batched variant of SubmitLocalCommand. */
	void SubmitLocalCommands(const TArray<FSeinCommand>& Commands);

	/** Explicit-turn variants. Used by the Sein.Net.TestPing console command
	 *  to drive deterministic turn IDs for the gate test, and by future
	 *  callers that already know the target turn. Player-input handlers should
	 *  use the no-TurnId overload instead. */
	void SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command);
	void SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Local player's slot, valid after the relay's AssignedPlayerID OnRep
	 *  fires (a few frames after PIE Play depending on replication latency).
	 *  Zero before that — callers should treat zero as "not yet assigned". */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Network")
	FSeinPlayerID GetLocalPlayerID() const { return LocalPlayerID; }

	/** Session-wide deterministic seed. Server generates once at first
	 *  PostLogin; replicated to clients via the relay. Sim PRNG MUST be
	 *  initialized from this exact value before tick 0 or rolls diverge. */
	int64 GetSessionSeed() const { return SessionSeed; }

	/** Server-side: how many slots are currently occupied. Used by the
	 *  per-turn completeness gate. */
	int32 GetActiveSlotCount() const { return RelayToSlot.Num(); }

	/** Server-only: spawn the lockstep relay for a given PlayerController
	 *  with the slot already chosen by the match-flow / lobby authority
	 *  (typically `ASeinGameMode::HandleStartingNewPlayer` after it sets
	 *  `SeinPC->SeinPlayerID`). This REPLACES the old auto-spawn-on-
	 *  PostLogin path, which independently sequenced slots via
	 *  NextSlotToAssign and could disagree with GameMode's match-settings-
	 *  driven binding when controllers connected in non-slot order — a
	 *  silent dual-source-of-truth bug.
	 *
	 *  Idempotent: if the PC already has a relay (e.g. seamless travel,
	 *  re-bind), the existing relay is re-stamped with the new slot
	 *  instead of double-spawning. */
	void ServerSpawnRelayForController(APlayerController* PC, FSeinPlayerID Slot);

	/** Called by ASeinNetRelay::OnRep_AssignedPlayerID on the owning client
	 *  when the slot + seed arrive. Binds the lockstep gate hooks but does
	 *  NOT start the sim — sim start is gated on Client_StartSession (server)
	 *  / StartLockstepSession (manual / lobby trigger). */
	void NotifyLocalSlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot, int64 Seed);

	/** Server-only: kick off the lockstep session. Fires Client_StartSession
	 *  on every connected relay (so all peers' sims start simultaneously, at
	 *  tick 0, with the gate engaged from the first frame) and starts the
	 *  server's own sim. Called from `Sein.Net.StartMatch` (Phase 2b) or
	 *  from a future lobby flow (Phase 3). Idempotent — already-running
	 *  sims are left alone. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void StartLockstepSession();

	/** Local-only: start the sim if it isn't running. Called by
	 *  Client_StartSession on receiving clients, and by StartLockstepSession
	 *  on the host. Public so the console command can drive it directly. */
	void StartLocalSession();

	/** Server-only accessor: the replay writer instance, valid between
	 *  StartLockstepSession and the eventual FinishRecording flush. nullptr
	 *  on clients (only the server captures the canonical turn stream).
	 *  Public so Sein.Net.SaveReplay can drive a manual flush. */
	USeinReplayWriter* GetReplayWriter() const { return ReplayWriter; }

	/** Relay lifecycle hooks called by ASeinNetRelay::BeginPlay/EndPlay on
	 *  whichever process the body runs in. Server tracks every relay; clients
	 *  remember which one is theirs. */
	void RegisterRelay(ASeinNetRelay* Relay);
	void UnregisterRelay(ASeinNetRelay* Relay);

	/** Server-side: a relay just received a client submission. Phase 1: stamp
	 *  the source player slot, buffer into per-turn map, fan out the assembled
	 *  turn once every connected slot has submitted for `TurnId`. */
	void ServerHandleSubmission(ASeinNetRelay* SourceRelay, int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Client-side: server delivered an assembled turn. Phase 1: log + fire
	 *  the test delegate. Phase 2 will drain into USeinWorldSubsystem at the
	 *  matching sim-tick boundary. */
	void ClientHandleTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Phase 0 visibility hooks — bind from console exec or test code. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTurnReceived, int32 /*TurnId*/, const TArray<FSeinCommand>& /*Commands*/);
	FOnTurnReceived OnTurnReceived;

	/** True if networking is enabled in settings AND the world has a real
	 *  NetDriver (NetMode != NM_Standalone). Public so callers can early-out
	 *  before constructing a payload. */
	bool IsNetworkingActive() const;

	/** Server-side accessor: every relay currently registered. Client-side:
	 *  contains only the local relay (or empty if not replicated yet). */
	const TArray<TWeakObjectPtr<ASeinNetRelay>>& GetRelays() const { return Relays; }

private:
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);

	/** True only on the server side. Cached at first relay-spawn / first
	 *  hook call; cheaper than re-querying NetMode every entry. */
	bool IsServer() const;

	/** Server-side: lazy-init SessionSeed on first call. Once set, never
	 *  changes for the life of this game-instance subsystem. */
	void EnsureSessionSeed();

	/** Server-side: check if every connected slot has submitted for `TurnId`;
	 *  if so, assemble + fan out via every relay's Client_ReceiveTurn. */
	void ServerCheckTurnComplete(int32 TurnId);

	/** Lockstep gate hook (Phase 2b). Bound on USeinWorldSubsystem when the
	 *  local slot is assigned. Returns true iff the assembled turn for `Turn`
	 *  is in ReceivedTurns, OR `Turn` is in the InputDelay-turns grace period
	 *  at session start (no submissions could exist for those). */
	bool ResolveTurnReady(int32 Turn);

	/** Lockstep drain hook (Phase 2b). Paired with ResolveTurnReady. Drains
	 *  `Turn`'s assembled commands into WorldSubsystem.PendingCommands and
	 *  removes the entry from ReceivedTurns. Called once per turn boundary. */
	void ConsumeTurn(int32 Turn);

	/** Subscribed to USeinWorldSubsystem::OnSimTickCompleted via
	 *  TickCompletedHandle. At every turn boundary, flushes pending outgoing
	 *  commands (or an empty heartbeat) to the server so the gate can
	 *  complete on every connected peer. */
	void OnSimTickCompleted(int32 CompletedTick);

	/** Read-helper: ticks-per-turn from settings, with sane fallback. */
	int32 GetTicksPerTurn() const;
	int32 GetInputDelayTurns() const;

	UPROPERTY()
	TArray<TWeakObjectPtr<ASeinNetRelay>> Relays;

	/** The local client's relay (also valid on the host, since the host has
	 *  its own PC + relay). Set when a relay registers itself whose owner
	 *  matches the local first PC. */
	TWeakObjectPtr<ASeinNetRelay> LocalRelay;

	/** Server-side: relay -> slot mapping. Source of truth for
	 *  authoritative "who sent this submission". */
	TMap<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID> RelayToSlot;

	/** Server-side: per-turn aggregation buffer. Inner map: PlayerID -> their
	 *  submitted commands for that turn. Once Inner.Num() == GetActiveSlotCount(),
	 *  the turn is fanned out and the entry purged. */
	TMap<int32, TMap<FSeinPlayerID, TArray<FSeinCommand>>> ServerTurnBuffer;

	/** Server-side: completed/dispatched turn IDs (so a stragglar's late
	 *  submission for an already-dispatched turn doesn't re-fan or stall). */
	TSet<int32> CompletedTurns;

	/** Local-client cache: this client's slot (replicated via relay). Zero
	 *  until OnRep_AssignedPlayerID fires. */
	FSeinPlayerID LocalPlayerID;

	/** Session-wide seed. On the server: generated once in EnsureSessionSeed
	 *  on first PostLogin. On clients: latched from the relay's replicated
	 *  property. */
	int64 SessionSeed = 0;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;

	/** Subscription to USeinWorldSubsystem::OnSimTickCompleted, set in
	 *  NotifyLocalSlotAssigned (after the world subsystem exists), cleared
	 *  in Deinitialize. Drives the per-turn heartbeat flush. */
	FDelegateHandle TickCompletedHandle;

	/** Client-side: assembled turns received from the server, awaiting
	 *  drainage at the matching sim-tick turn boundary. Keyed by turn ID. */
	TMap<int32, TArray<FSeinCommand>> ReceivedTurns;

	/** Client-side: locally captured commands buffered until the next turn
	 *  boundary, when they're flushed to the server (with the appropriate
	 *  outgoing TurnId stamp) along with an implicit heartbeat. */
	TArray<FSeinCommand> PendingOutgoingCommands;

	/** Client-side: highest TurnId we've already submitted for. Tracked so a
	 *  single-turn worth of OnSimTickCompleted ticks doesn't double-submit
	 *  the same heartbeat. -1 means "never submitted yet". */
	int32 LastSubmittedTurn = -1;

	/** Server-only: captures every dispatched turn for offline replay
	 *  reconstruction. Created lazily in StartLockstepSession, flushed in
	 *  Deinitialize (or via Sein.Net.SaveReplay). Null on clients. */
	UPROPERTY()
	TObjectPtr<USeinReplayWriter> ReplayWriter;
};
