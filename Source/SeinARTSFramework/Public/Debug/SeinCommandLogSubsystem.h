/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandLogSubsystem.h
 * @brief   Debug subsystem that captures command transactions from the sim
 *          and provides data for the HUD overlay to render.
 *          Toggle: Sein.Commands.ShowLog  |  Clear: Sein.Commands.ClearLog
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "GameplayTagContainer.h"
#include "SeinCommandLogSubsystem.generated.h"

class USeinWorldSubsystem;

/**
 * A single entry in the debug command log.
 */
USTRUCT()
struct FSeinCommandLogEntry
{
	GENERATED_BODY()

	int32 Tick = 0;
	int32 PlayerIndex = -1;
	int32 EntityIndex = -1;
	FGameplayTag CommandType;
	FString Description;
	FColor DisplayColor = FColor::White;
};

/**
 * Data-only debug subsystem: captures commands via OnCommandsProcessing delegate.
 * Rendering is done by ASeinHUD which reads from this subsystem.
 */
UCLASS()
class SEINARTSFRAMEWORK_API USeinCommandLogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Whether the overlay should be drawn by the HUD. */
	bool bShowOverlay = false;

	/** Clear the log. */
	void ClearLog() { LogEntries.Empty(); }

	/** Read log entries (most recent last). */
	const TArray<FSeinCommandLogEntry>& GetLogEntries() const { return LogEntries; }

	/** Max entries to keep. */
	int32 MaxLogEntries = 256;

	/** Max entries to display on screen. */
	int32 MaxDisplayEntries = 24;

private:
	TArray<FSeinCommandLogEntry> LogEntries;
	TWeakObjectPtr<USeinWorldSubsystem> SimSubsystem;
	FDelegateHandle CommandsDelegateHandle;

	void OnCommandsProcessing(int32 Tick, const TArray<FSeinCommand>& Commands);
	static FString DescribeCommand(const FSeinCommand& Cmd);
	static FColor GetCommandColor(FGameplayTag CommandType);
};
