/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyViewModel.h
 * @brief   ViewModel exposing the replicated lobby state to designer Widget BPs.
 *
 * Phase 3c lobby UI surface. Mirrors the established `USeinPlayerViewModel`
 * pattern: a UObject wrapper around the underlying replicated lobby actor,
 * with BP-friendly accessors + a multicast `OnLobbyChanged` event fired on
 * RepNotify. Designers extend `USeinUserWidget` and bind to this view model
 * to author the actual lobby Widget BPs (`WBP_Lobby`, `WBP_LobbySlotTile`,
 * `WBP_FactionPicker`) without touching framework C++.
 *
 * Responsibilities:
 *  - Resolve the GI-level `USeinLobbySubsystem` and its replicated
 *    `ASeinLobbyState` actor (waits for replication on clients).
 *  - Expose `Slots`, `LocalSlotIndex`, `bIsHost`, `bCanStartMatch` to BP.
 *  - Fire `OnLobbyChanged` when the actor's `OnLobbyStateChanged` fires
 *    (replication or server-side commit).
 *
 * The verbs (claim slot, start match, leave) live on `USeinLobbyBPFL`, not
 * here — view model is read-only by convention.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/SeinPlayerID.h"
#include "SeinLobbyState.h"
#include "SeinLobbyViewModel.generated.h"

class USeinLobbySubsystem;
class ASeinLobbyState;
class APlayerController;
class UWorld;

/** Broadcast when the lobby's replicated state changes (RepNotify or server commit). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyChanged);

/** Broadcast when the local player's slot binding changes (relay
 *  AssignedPlayerID OnRep, or initial binding latch). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLocalSlotChanged, FSeinPlayerID, NewLocalSlot);

UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinLobbyViewModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Lifecycle ==========

	/** Initialize against a world. Resolves the lobby subsystem + binds to the
	 *  replicated lobby actor's change delegate. Call from the owning widget's
	 *  Construct, or use `USeinLobbyBPFL::SeinGetOrCreateLobbyViewModel`
	 *  to get a cached singleton via `USeinUISubsystem`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Lobby")
	void Initialize(UWorld* InWorld);

	/** Tear down delegate bindings. Called on world end. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Lobby")
	void Shutdown();

	// ========== Reads ==========

	/** Snapshot of the replicated lobby slot array. Updated whenever the
	 *  underlying actor's `OnLobbyStateChanged` fires. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	const TArray<FSeinLobbySlotState>& GetSlots() const { return CachedSlots; }

	/** Find a slot by its index (1-based). Returns false if not present. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool TryGetSlot(int32 SlotIndex, FSeinLobbySlotState& OutSlot) const;

	/** The local player's claimed slot. Zero (Neutral) until the relay's
	 *  AssignedPlayerID OnRep arrives. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	FSeinPlayerID GetLocalSlot() const { return LocalSlotID; }

	/** True iff this client is the listen-server / dedicated-server host. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool IsHost() const;

	/** True iff the host can issue Start Match (today: at least one
	 *  Human-claimed slot). Designers override by subclassing the BPFL or
	 *  binding to `OnLobbyChanged` and computing their own gate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool CanStartMatch() const;

	/** True if a snapshot has been published (server only — false on clients). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Lobby")
	bool HasPublishedMatchSnapshot() const;

	// ========== Events ==========

	/** Fires when the lobby's replicated slot array changes. Bind from BP
	 *  to refresh the slot panel. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Lobby")
	FOnLobbyChanged OnLobbyChanged;

	/** Fires when the local player's slot binding changes. Bind to update
	 *  faction-picker selection state, "ready" button enable state, etc. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Lobby")
	FOnLocalSlotChanged OnLocalSlotChanged;

private:
	/** Resolve subsystem from the cached world. nullptr after Shutdown or if
	 *  GI is mid-teardown. */
	USeinLobbySubsystem* GetLobbySubsystem() const;

	/** Pull the latest snapshot from the lobby actor + fire `OnLobbyChanged`. */
	void RefreshFromActor();

	/** Hook bound to `ASeinLobbyState::OnLobbyStateChanged`. */
	void HandleLobbyStateChanged();

	/** Polled each tick (cheap) to detect when the local relay's
	 *  AssignedPlayerID has been replicated, since there's no delegate from
	 *  the relay → view model. Could be replaced with a NetSubsystem
	 *  delegate later. */
	void HandleLocalSlotPoll();

	UPROPERTY()
	TWeakObjectPtr<UWorld> CachedWorld;

	UPROPERTY()
	TWeakObjectPtr<ASeinLobbyState> BoundActor;

	UPROPERTY()
	TArray<FSeinLobbySlotState> CachedSlots;

	UPROPERTY()
	FSeinPlayerID LocalSlotID;

	FDelegateHandle LobbyChangedHandle;
	FTSTicker::FDelegateHandle LocalSlotPollHandle;
};
