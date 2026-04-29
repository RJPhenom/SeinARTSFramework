/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbySubsystem.h
 * @brief   Game-instance subsystem managing pre-match lobby state.
 *
 * Phase 3b. Server-authoritative lobby that lets connecting players claim a
 * slot + pick a faction before the lockstep session starts. Coexists with
 * USeinNetSubsystem (which owns the lockstep wire); both live at the GI
 * scope so they survive map travel between lobby + gameplay maps.
 *
 * Responsibilities:
 *  - On the server, spawn one ASeinLobbyState actor per session and seed it
 *    with default slots (count from USeinARTSCoreSettings::MaxPlayers, or
 *    an explicit InitializeLobby call from a future lobby Widget BP).
 *  - Hook FGameModeEvents::PostLogin: assign joining controllers to the next
 *    free Open slot (current convention: slot N → PlayerID N), mark Claimed.
 *  - Hook FGameModeEvents::Logout: release the leaving controller's slot.
 *  - Receive ServerHandleSlotClaim from ASeinNetRelay::Server_RequestSlotClaim
 *    (clients changing slot / faction mid-lobby).
 *  - Build an FSeinMatchSettings snapshot on demand (called from
 *    Sein.Net.StartMatch and the future map-travel handoff).
 *  - Publish the snapshot so ASeinGameMode::ResolveMatchSettingsForWorld can
 *    read it as the highest-priority match-settings source.
 *
 * Coexistence with single-map PIE flow (current state, pre-3c):
 *  - ASeinGameMode::InitGame runs BEFORE any controller has connected, so the
 *    lobby snapshot is empty here; GameMode falls back to ASeinWorldSettings
 *    (existing behavior preserved).
 *  - Players connect → lobby state populated → snapshot becomes meaningful.
 *  - Sein.Net.StartMatch builds the snapshot but the in-flight match is
 *    already running off WorldSettings — Phase 3c map-travel will surface
 *    the snapshot in the new map's GameMode::InitGame chain.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "SeinLobbyState.h"
#include "SeinLobbySubsystem.generated.h"

class APlayerController;
class AController;
class AGameModeBase;
class ASeinNetRelay;

UCLASS(Config = Game)
class SEINARTSNET_API USeinLobbySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Optional gameplay map to ServerTravel to when StartLockstepSession is
	 * called via `USeinLobbyBPFL::SeinRequestStartMatch` with travel enabled.
	 * Empty = no travel (host runs the match in the current map). Configure
	 * per-project via Project Settings → SeinARTS, or set at runtime from a
	 * lobby Widget BP via `Set Gameplay Map`.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Lobby",
		meta = (DisplayName = "Gameplay Map"))
	TSoftObjectPtr<UWorld> GameplayMap;

	// ========== Server API ==========

	/** Server-only: (re)initialize the lobby with `SlotCount` Open slots. Idempotent
	 *  in the sense that re-initializing wipes existing claims — call only on
	 *  fresh sessions. If `SlotCount` <= 0, defaults to USeinARTSCoreSettings::MaxPlayers. */
	void InitializeLobby(int32 SlotCount = 0);

	/** Server-only: route a client's claim request from the relay. Validates
	 *  slot existence + occupancy + faction range, commits to the actor, then
	 *  re-replicates. Returns true on accept. */
	bool ServerHandleSlotClaim(APlayerController* PC, int32 SlotIndex, FSeinFactionID Faction);

	/** Server-only: build a fully-populated FSeinMatchSettings from the current
	 *  lobby state. Called by Sein.Net.StartMatch and (in Phase 3c) the
	 *  map-travel handoff. PC names / AI profiles flow through unchanged.
	 *
	 *  CapturedSettingsOut is overwritten with the snapshot. Returns true iff
	 *  the lobby has at least one Human or AI slot — empty lobby is treated as
	 *  "fall back to WorldSettings" by callers. */
	bool BuildMatchSettingsSnapshot(FSeinMatchSettings& CapturedSettingsOut) const;

	/** Server-only: take a snapshot now and store it as the GI-scoped match
	 *  settings override. Game mode's ResolveMatchSettingsForWorld picks this
	 *  up first. Called by Sein.Net.StartMatch. */
	void PublishMatchSettingsSnapshot();

	/** Server-only: full match-start flow used by `USeinLobbyBPFL::SeinRequestStartMatch`.
	 *  Publishes the snapshot, then either:
	 *   - if `bTravelToGameplayMap=true` AND `GameplayMap` is set, calls
	 *     `World::ServerTravel(GameplayMap)` (the new map's GameMode picks
	 *     up the snapshot in InitGame), OR
	 *   - else calls `USeinNetSubsystem::StartLockstepSession()` in-place.
	 *
	 *  Returns true on accept, false if rejected (no Human-claimed slot or
	 *  travel requested but `GameplayMap` empty). */
	bool ServerStartMatch(bool bTravelToGameplayMap);

	// ========== Read API (server + client) ==========

	/** True iff a snapshot has been published (PublishMatchSettingsSnapshot
	 *  has been called at least once this GI lifetime). */
	bool HasPublishedSnapshot() const { return bSnapshotPublished; }

	/** Snapshot accessor. Empty (default-constructed) until
	 *  PublishMatchSettingsSnapshot fires. */
	const FSeinMatchSettings& GetPublishedSnapshot() const { return PublishedSnapshot; }

	/** Current replicated lobby actor; null on the client until first
	 *  replication arrives, null on the server until InitializeLobby spawns it. */
	ASeinLobbyState* GetLobbyState() const { return LobbyStateActor.Get(); }

	// ========== Replication callbacks (from ASeinLobbyState) ==========

	void NotifyLobbyStateActorBeginPlay(ASeinLobbyState* Actor);
	void NotifyLobbyStateActorEndPlay(ASeinLobbyState* Actor);

private:
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);

	bool IsServer() const;

	/** Server-only: ensure the ASeinLobbyState actor exists in the current
	 *  world. Spawns lazily on first need; idempotent. */
	void EnsureLobbyActor();

	/** Server-only: pick the lowest-index Open slot not already claimed. Returns
	 *  0 if none free. */
	int32 PickNextFreeSlot() const;

	/** Server-only: commit a slot's state and broadcast the change. */
	void CommitSlotState(int32 SlotIndex, const FSeinLobbySlotState& NewState);

	UPROPERTY()
	TWeakObjectPtr<ASeinLobbyState> LobbyStateActor;

	/** Server-side: PC ↔ slot mapping. Drives Logout-side release. */
	TMap<TWeakObjectPtr<APlayerController>, int32> ControllerToSlot;

	/** Cached published snapshot. Updated on PublishMatchSettingsSnapshot. */
	FSeinMatchSettings PublishedSnapshot;
	bool bSnapshotPublished = false;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
};
