/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyBPFL.h
 * @brief   Blueprint surface for the lobby flow — verbs designer Widget BPs
 *          call from button-click events.
 *
 * Phase 3c. Designers extend `USeinUserWidget`, drop a `USeinLobbyViewModel`
 * (via `SeinGetOrCreateLobbyViewModel`) for read-side data, and call the
 * verbs here on click handlers. Framework owns the wiring; project owns the
 * UMG composition / styling.
 *
 * All verbs are world-context-aware so they work from any UMG event graph
 * without a hand-passed `World`. Each routes through the appropriate
 * subsystem (NetSubsystem for the relay-RPC paths, LobbySubsystem for
 * lobby-state reads, etc.) so no game code needs to know which subsystem
 * owns what.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinFactionID.h"
#include "SeinLobbyBPFL.generated.h"

class USeinLobbyViewModel;

UCLASS(meta = (DisplayName = "SeinARTS Lobby Library"))
class SEINARTSUITOOLKIT_API USeinLobbyBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ========== View-model accessor ==========

	/** Get (or lazily create + initialize) the lobby view model for the
	 *  current world. Cached on the UISubsystem so multiple widgets share
	 *  a single instance + change-event subscription. Designers grab this
	 *  in the lobby widget's Construct event and use it as their data
	 *  source for the slot panel. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Or Create Lobby View Model"))
	static USeinLobbyViewModel* SeinGetOrCreateLobbyViewModel(const UObject* WorldContextObject);

	// ========== Verbs ==========

	/** Request that the local player be moved to `SlotIndex` with the given
	 *  faction. Routes through the local relay's Server_RequestSlotClaim
	 *  RPC. Server validates + replicates the new lobby state to all peers
	 *  via the always-relevant `ASeinLobbyState` actor. Returns true if the
	 *  request was actually sent (false = no relay yet, retry next frame). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Slot Claim"))
	static bool SeinRequestSlotClaim(const UObject* WorldContextObject, int32 SlotIndex, FSeinFactionID FactionID);

	/** Host-only: snapshot the current lobby state into `FSeinMatchSettings`,
	 *  publish it on the GI as the next match's source-of-truth, then either
	 *  (a) start the lockstep session in-place if `bTravelToGameplayMap=false`,
	 *  or (b) `ServerTravel` to the configured gameplay map (set
	 *  `USeinLobbySubsystem::GameplayMap`) and let the new map's GameMode
	 *  consume the snapshot in InitGame.
	 *
	 *  Returns true if the request was accepted (host + at least one
	 *  Human-claimed slot), false if rejected. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Start Match"))
	static bool SeinRequestStartMatch(const UObject* WorldContextObject);

	/** Disconnect the local player from the session. Host: tears down the
	 *  listen server (every peer disconnects). Client: leaves the session,
	 *  returns to the project's main menu map (if configured). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request Leave Lobby"))
	static void SeinRequestLeaveLobby(const UObject* WorldContextObject);
};
