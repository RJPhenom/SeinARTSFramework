/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetRelay.h
 * @brief   Per-PlayerController relay actor carrying the lockstep wire.
 *
 * One relay is server-spawned for every connected APlayerController and
 * owned by it (so the client legitimately owns the actor it's RPC'ing
 * through). The actor exposes two RPCs:
 *
 *   Server_SubmitCommands    — client -> server. The owning client packs its
 *                              locally captured FSeinCommands for an upcoming
 *                              turn; server forwards to USeinNetSubsystem for
 *                              per-turn aggregation.
 *
 *   Client_ReceiveTurn       — server -> owning client. Once the host has
 *                              gathered every player's submissions for a
 *                              turn, it iterates every relay and unicasts
 *                              the assembled turn back. Reaches every
 *                              connected player (host iterates all relays;
 *                              the host's own relay reaches the host process
 *                              by RPC-loopback).
 *
 * No multicast — fan-out is done by iterating per-PC relays. Same total
 * bandwidth as a true Multicast_BroadcastTurn, but no Always-Relevant
 * singleton needed and ownership stays clean (each client owns only its own
 * relay).
 *
 * Phase 0: the body of these RPCs just logs to LogSeinNet. Phase 2 wires
 * them to the turn-buffer in USeinWorldSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "SeinNetRelay.generated.h"

class USeinNetSubsystem;

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Net Relay"))
class SEINARTSNET_API ASeinNetRelay : public AActor
{
	GENERATED_BODY()

public:
	ASeinNetRelay();

	/** Sim-side player slot (1..N) the server has assigned to this relay's
	 *  owner. Stamped server-side at spawn and replicated to the owning client.
	 *  Phase 1 — sequential by PostLogin order; Phase 2 lobby flow will let
	 *  designers pick / shuffle slots before StartMatch. */
	UPROPERTY(ReplicatedUsing = OnRep_AssignedPlayerID, BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinPlayerID AssignedPlayerID;

	/** Session-wide deterministic seed. Same value on every relay (one per PC,
	 *  same value across all). Lockstep-critical: every client's PRNG MUST
	 *  initialize from this exact value before tick 0, or rolls diverge from
	 *  the first frame. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 SessionSeed = 0;

	/** Client -> server. Owning client packs its commands for `TurnId`; server
	 *  forwards to USeinNetSubsystem for aggregation. Reliable: lockstep cannot
	 *  tolerate dropped commands. The server treats the source relay's
	 *  `AssignedPlayerID` as the authoritative sender (the FSeinCommand's
	 *  PlayerID field is rewritten on the server before fan-out). */
	UFUNCTION(Server, Reliable)
	void Server_SubmitCommands(int32 TurnId, const TArray<FSeinCommand>& Commands);

	/** Server -> owning client. Delivered after the host has assembled every
	 *  player's commands for `TurnId`. Client hands the unified turn to its
	 *  USeinWorldSubsystem (Phase 2 sim integration). */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveTurn(int32 TurnId, const TArray<FSeinCommand>& Commands);

	UFUNCTION()
	void OnRep_AssignedPlayerID();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Resolve the owning game-instance subsystem on whichever side this body
	 *  is executing. nullptr only if the GI was torn down ahead of the actor. */
	USeinNetSubsystem* GetNetSubsystem() const;
};
