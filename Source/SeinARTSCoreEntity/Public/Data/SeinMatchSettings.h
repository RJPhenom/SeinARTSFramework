/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettings.h
 * @brief   Match-level configuration primitive (DESIGN §18). `FSeinMatchSettings`
 *          is the runtime struct snapshotted into `USeinWorldSubsystem` at
 *          StartMatch (immutable after). `USeinMatchSettings` (same dir) is
 *          the lobby-preset data asset wrapping a default value.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinMatchSettings.generated.h"

/**
 * Match flow state (DESIGN §18).
 * Transitions are command-driven (lockstep-deterministic).
 */
UENUM(BlueprintType)
enum class ESeinMatchState : uint8
{
	/** Pre-match, players joining, settings configurable. */
	Lobby,
	/** PreMatchCountdown running, commands blocked. */
	Starting,
	/** Normal sim execution. */
	Playing,
	/** Sim halted per pause mode. */
	Paused,
	/** Victory/defeat declared, cleanup phase. */
	Ending,
	/** Match over, replay saved, ready to return to lobby. */
	Ended,
};

/**
 * Pause mode (DESIGN §18). Picked per `SetSimPaused` call; default comes from
 * `FSeinMatchSettings::DefaultPauseMode`.
 */
UENUM(BlueprintType)
enum class ESeinPauseMode : uint8
{
	/** Sim halts; commands accumulate; resume drains them. Campaign / single-player default. */
	Tactical,
	/** Sim halts; commands rejected with `Command.Reject.SimPaused`. Cleaner for competitive vote-pauses. */
	Hard,
};

/**
 * Behavior when the host disconnects mid-match (DESIGN §18).
 * Framework provides the enum + command hooks; network-layer election is UE's job.
 */
UENUM(BlueprintType)
enum class ESeinHostDropAction : uint8
{
	/** End the match immediately on host drop. */
	EndMatch,
	/** Sim pauses until new host elected. */
	PauseUntilNewHost,
	/** New host auto-elected (typically lowest-latency remaining). */
	AutoMigrate,
	/** Auto-migrate + spawn AI for the dropped host's (and any other dropped player's) entities. */
	AITakeover,
};

/**
 * Resource-sharing mode across allied / team players (DESIGN §6 + §18).
 * V1 ships `PerPlayer` (each player has their own balance — typical competitive)
 * and `SharedPool` (aliased balances across `Permission.ResourceShare` pairs).
 */
UENUM(BlueprintType)
enum class ESeinResourceSharingMode : uint8
{
	/** Each player maintains their own resource balance — default. */
	PerPlayer,
	/** Players with `SeinARTS.Diplomacy.Permission.ResourceShare` share one pool. */
	SharedPool,
};

/**
 * Runtime match configuration snapshot. Read via `USeinMatchSettingsBPFL::Get`
 * after match start; mutation after that point is forbidden (desync-safe).
 *
 * Designer extensions: populate `CustomSettings` with a project-specific USTRUCT
 * via `FInstancedStruct`; framework reads none of it — your BPFLs pull it out.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchSettings
{
	GENERATED_BODY()

	// Resource & combat ---------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	ESeinResourceSharingMode ResourceSharing = ESeinResourceSharingMode::PerPlayer;

	/** Allies can dispatch broker orders to each other's entities (gated by
	 *  `SeinARTS.Diplomacy.Permission.CommandSharing`). Widens §5's broker
	 *  ownership invariant when true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	bool bAlliedCommandSharing = false;

	/** When false, `SeinApplyDamage` skips targets whose owner is allied to
	 *  the damage source (`Permission.Allied`). Designer ability code can
	 *  override via `SeinMutateAttribute` for accidental-fire mechanics. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	bool bFriendlyFire = false;

	// Match flow ----------------------------------------------------------

	/** Pre-match countdown in sim-seconds. Sim rejects player commands while
	 *  state == Starting. Fractional values round up to the nearest tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (ClampMin = "0.0"))
	FFixedPoint PreMatchCountdown = FFixedPoint::FromInt(3);

	/** Minimum match duration in sim-seconds before concede / end-match is
	 *  accepted. 0 = no minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (ClampMin = "0.0"))
	FFixedPoint MinMatchDuration = FFixedPoint::Zero;

	/** Default pause mode when `SetSimPaused` is called without an explicit
	 *  override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	ESeinPauseMode DefaultPauseMode = ESeinPauseMode::Tactical;

	// Host / drop handling ------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	ESeinHostDropAction HostDropAction = ESeinHostDropAction::PauseUntilNewHost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	bool bEnableAITakeoverForDroppedPlayers = false;

	// Diplomacy -----------------------------------------------------------

	/** If false, `FSeinCommand::ModifyDiplomacy` is rejected after match start
	 *  — relations locked at match settings snapshot. Typical for competitive
	 *  team games; enable for grand-strategy / campaign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	bool bAllowMidMatchDiplomacy = false;

	// Spectator -----------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	bool bAllowSpectators = true;

	// Designer extensions --------------------------------------------------

	/** Opaque to the framework — project BPFLs / scenarios extract via
	 *  `FInstancedStruct`'s native break. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (SeinDeterministicOnly))
	FInstancedStruct CustomSettings;
};

/**
 * Lobby-preset asset wrapping a `FSeinMatchSettings` default plus UI metadata.
 * Designers author multiple presets ("Standard 1v1", "Casual 2v2 AI", "Campaign
 * Mission 3", ...) and the lobby UI picks one. (Named "Preset" to avoid UHT's
 * UClass/USTRUCT name collision with `FSeinMatchSettings`.)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Match Settings Preset"))
class SEINARTSCOREENTITY_API USeinMatchSettingsPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	TObjectPtr<UTexture2D> Icon;

	/** Default settings value applied to the sim on StartMatch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	FSeinMatchSettings Settings;
};
