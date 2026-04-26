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

UCLASS()
class SEINARTSNET_API USeinNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Locally captured command entry point. In Standalone (or when networking
	 *  is disabled in settings) the command bypasses the relay and goes
	 *  directly into USeinWorldSubsystem's command buffer — single-player is
	 *  zero-overhead. In a networked session, forwards to the local relay's
	 *  Server_SubmitCommands; the server's AssignedPlayerID stamps the sender,
	 *  so callers don't need to fill `Command.PlayerID` (server overwrites). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Network")
	void SubmitLocalCommand(int32 TurnId, const FSeinCommand& Command);

	/** Batched variant — useful when multiple commands are captured between
	 *  turn boundaries. Same gating as SubmitLocalCommand. */
	void SubmitLocalCommands(int32 TurnId, const TArray<FSeinCommand>& Commands);

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

	/** Called by ASeinNetRelay::OnRep_AssignedPlayerID on the owning client
	 *  when the slot + seed arrive. */
	void NotifyLocalSlotAssigned(ASeinNetRelay* Relay, FSeinPlayerID Slot, int64 Seed);

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

	/** Server-side: assign the next free slot to a freshly-spawned relay,
	 *  stamp its AssignedPlayerID + SessionSeed (replicated to its owning
	 *  client), and remember the mapping locally. */
	FSeinPlayerID ServerAssignNextSlot(ASeinNetRelay* Relay);

	/** Server-side: lazy-init SessionSeed on first call. Once set, never
	 *  changes for the life of this game-instance subsystem. */
	void EnsureSessionSeed();

	/** Server-side: check if every connected slot has submitted for `TurnId`;
	 *  if so, assemble + fan out via every relay's Client_ReceiveTurn. */
	void ServerCheckTurnComplete(int32 TurnId);

	UPROPERTY()
	TArray<TWeakObjectPtr<ASeinNetRelay>> Relays;

	/** The local client's relay (also valid on the host, since the host has
	 *  its own PC + relay). Set when a relay registers itself whose owner
	 *  matches the local first PC. */
	TWeakObjectPtr<ASeinNetRelay> LocalRelay;

	/** Server-side: relay -> slot mapping. Source of truth for
	 *  authoritative "who sent this submission". */
	TMap<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID> RelayToSlot;

	/** Server-side: the next slot to hand out. Starts at 1 (slot 0 = neutral). */
	uint8 NextSlotToAssign = 1;

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
};
