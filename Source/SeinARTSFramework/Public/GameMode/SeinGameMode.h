/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.h
 * @brief   RTS game mode that wires up the player shell (controller, camera,
 *          HUD) and manages player registration with the simulation.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Player/SeinPlayerController.h"
#include "SeinGameMode.generated.h"

class ASeinPlayerStart;

/**
 * Default RTS game mode.
 *
 * Sets default pawn/controller/HUD classes to the Sein RTS variants.
 * Assigns FSeinPlayerIDs to connecting players and registers them with
 * the deterministic simulation via USeinWorldSubsystem::RegisterPlayer().
 *
 * Designers subclass this in Blueprint to configure starting resources,
 * factions, team assignments, and match flow.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASeinGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// ========== Configuration ==========

	/** Maximum number of players. Players beyond this are rejected. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	int32 MaxPlayers = 8;

	/** Default faction assigned to players when no faction is specified. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	FSeinFactionID DefaultFactionID = FSeinFactionID(1);

	/** Default dispatch mode for the player controller. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	ESeinCommandDispatchMode DefaultDispatchMode = ESeinCommandDispatchMode::LeaderDriven;

	/** Whether to auto-start the simulation when the first player joins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	bool bAutoStartSimulation = true;

	/**
	 * Starting-resource override map applied on top of the faction's ResourceKit
	 * at player registration. Keyed by resource tag (SeinARTS.Resource.*).
	 * Leave empty to use faction-kit defaults. Match-settings-level tweaks will
	 * eventually supersede this per §18; see PLAN.md Session 5.3.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode",
		meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, float> StartingResources;

	// ========== Runtime State ==========

	/** Next player ID to assign. Incremented per player. 0 = Neutral (reserved). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	uint8 NextPlayerIDValue = 1;

	// ========== Helpers ==========

	/** Manually register a player with a specific ID and faction. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	void RegisterPlayerWithSim(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/**
	 * Find a SeinPlayerStart for the given player slot.
	 * If SlotIndex is 0, returns the first unclaimed start.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	ASeinPlayerStart* FindPlayerStartForSlot(int32 SlotIndex) const;

	/**
	 * Spawn the start entity defined on a SeinPlayerStart for the given player.
	 * Called automatically during player registration if the start has a SpawnEntity set.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	void SpawnStartEntity(ASeinPlayerStart* PlayerStart, FSeinPlayerID PlayerID);

protected:
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Whether simulation has been started. */
	bool bSimulationStarted = false;

	/** Tracks which SeinPlayerStart actors have been claimed, keyed by PlayerSlot. */
	UPROPERTY()
	TMap<int32, FSeinPlayerID> ClaimedSlots;

	/**
	 * Maps player IDs to their assigned slot index.
	 * Set by lobby/matchmaker before travel, or auto-assigned on connect.
	 * Key = FSeinPlayerID value, Value = target PlayerSlot.
	 * If empty, slots are assigned in order.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	TMap<uint8, int32> PreAssignedSlots;
};
