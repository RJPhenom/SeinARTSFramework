/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommand.h
 * @brief   Deterministic command system. Commands are issued by players/AI
 *          and processed during the CommandProcessing tick phase.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinCommand.generated.h"

/** Types of commands that can be issued to entities. */
UENUM(BlueprintType)
enum class ESeinCommandType : uint8
{
	/** Activate an ability identified by gameplay tag */
	ActivateAbility,
	/** Cancel the currently active ability */
	CancelAbility,
	/** Queue a unit or research in a production building */
	QueueProduction,
	/** Cancel a specific item in the production queue */
	CancelProduction,
	/** Set a building's rally point */
	SetRallyPoint,
	/** Ping a location (processed by sim, emits visual event so all players see it) */
	Ping,

	// --- Observer commands (logged for replays, skipped by sim) ---

	/** Periodic camera position snapshot for replay reconstruction */
	CameraUpdate,
	/** Player changed their selection and/or active focus */
	SelectionChanged,
};

/**
 * A single deterministic command from a player or AI to an entity.
 * Fully value-typed and serializable for lockstep networking.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCommand
{
	GENERATED_BODY()

	/** Player who issued the command */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FSeinPlayerID PlayerID;

	/** Entity this command targets */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FSeinEntityHandle EntityHandle;

	/** What kind of command this is */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	ESeinCommandType CommandType = ESeinCommandType::ActivateAbility;

	/** Gameplay tag identifying the ability or archetype */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FGameplayTag AbilityTag;

	/** Optional target entity (e.g., attack target) */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FSeinEntityHandle TargetEntity;

	/** Optional target location (e.g., move destination, rally point) */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FFixedVector TargetLocation;

	/** Sim tick this command is intended for */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	int32 Tick = 0;

	/** Index into the production queue (for CancelProduction) */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	int32 QueueIndex = -1;

	/** Whether to queue (append) rather than replace the current ability */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	bool bQueueCommand = false;

	/** Auxiliary world location (e.g., formation endpoint for drag orders) */
	UPROPERTY(BlueprintReadOnly, Category = "Command")
	FFixedVector AuxLocation;

	// --- Observer data (used by CameraUpdate / SelectionChanged, ignored by sim) ---

	/** Auxiliary fixed-point value A (camera yaw for CameraUpdate) */
	UPROPERTY(BlueprintReadOnly, Category = "Command|Observer")
	FFixedPoint AuxA;

	/** Auxiliary fixed-point value B (camera zoom distance for CameraUpdate) */
	UPROPERTY(BlueprintReadOnly, Category = "Command|Observer")
	FFixedPoint AuxB;

	/** Entity list (selected entities for SelectionChanged) */
	UPROPERTY(BlueprintReadOnly, Category = "Command|Observer")
	TArray<FSeinEntityHandle> EntityList;

	/** Active focus index within selection (-1 = all, 0+ = index into EntityList) */
	UPROPERTY(BlueprintReadOnly, Category = "Command|Observer")
	int32 ActiveFocusIndex = -1;

	// --- Helpers ---

	/** Returns true if this is an observer command (CameraUpdate, SelectionChanged) that the sim should skip. */
	bool IsObserverCommand() const;

	// --- Factory methods ---

	/** Create an ability activation command, optionally with a target entity and location. */
	static FSeinCommand MakeAbilityCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag Tag,
		FSeinEntityHandle Target = FSeinEntityHandle::Invalid(),
		FFixedVector Location = FFixedVector());

	/** Create a cancel-ability command. */
	static FSeinCommand MakeCancelCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity);

	/** Create a production queue command (unit or research). */
	static FSeinCommand MakeProductionCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Building,
		FGameplayTag ArchetypeTag);

	/** Create a rally point command. */
	static FSeinCommand MakeRallyPointCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Building,
		FFixedVector Location);

	/** Create an ability command with queue and formation support. */
	static FSeinCommand MakeAbilityCommandEx(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag Tag,
		FSeinEntityHandle Target,
		FFixedVector Location,
		bool bQueue,
		FFixedVector FormationEnd = FFixedVector());

	/** Create a ping command (visible to all players). */
	static FSeinCommand MakePingCommand(
		FSeinPlayerID Player,
		FFixedVector Location,
		FSeinEntityHandle OptionalTarget = FSeinEntityHandle::Invalid());

	// --- Observer command factories (for replay reconstruction) ---

	/** Create a camera snapshot command. */
	static FSeinCommand MakeCameraUpdateCommand(
		FSeinPlayerID Player,
		FFixedVector PivotLocation,
		FFixedPoint Yaw,
		FFixedPoint ZoomDistance);

	/** Create a selection changed command. */
	static FSeinCommand MakeSelectionChangedCommand(
		FSeinPlayerID Player,
		const TArray<FSeinEntityHandle>& SelectedEntities,
		int32 InActiveFocusIndex = -1);
};

/**
 * Collects commands for a single sim tick. Consumed and cleared each frame
 * during the CommandProcessing phase.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinCommandBuffer
{
	GENERATED_BODY()

	/** Buffered commands for this tick */
	UPROPERTY()
	TArray<FSeinCommand> Commands;

	/** Enqueue a command. */
	void AddCommand(const FSeinCommand& Cmd);

	/** Remove all buffered commands. */
	void Clear();

	/** Number of buffered commands. */
	int32 Num() const;

	/** Read-only access to the command array. */
	const TArray<FSeinCommand>& GetCommands() const;
};
