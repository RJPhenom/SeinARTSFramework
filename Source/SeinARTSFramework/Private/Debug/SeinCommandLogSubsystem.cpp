/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandLogSubsystem.cpp
 * @brief   Debug command log — data capture only, HUD does the rendering.
 */

#include "Debug/SeinCommandLogSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCommandLog, Log, All);

// ==================== Console Commands ====================

static FAutoConsoleCommand CmdToggleCommandLog(
	TEXT("SeinARTS.Debug.CommandLog"),
	TEXT("Toggle the SeinARTS command transaction log overlay"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine) return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				if (USeinCommandLogSubsystem* Sub = Context.World()->GetSubsystem<USeinCommandLogSubsystem>())
				{
					Sub->bShowOverlay = !Sub->bShowOverlay;
					UE_LOG(LogSeinCommandLog, Log, TEXT("Command log overlay: %s"),
						Sub->bShowOverlay ? TEXT("ON") : TEXT("OFF"));
				}
			}
		}
	})
);

static FAutoConsoleCommand CmdClearCommandLog(
	TEXT("SeinARTS.Debug.CommandLog.Clear"),
	TEXT("Clear the SeinARTS command transaction log"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine) return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				if (USeinCommandLogSubsystem* Sub = Context.World()->GetSubsystem<USeinCommandLogSubsystem>())
				{
					Sub->ClearLog();
					UE_LOG(LogSeinCommandLog, Log, TEXT("Command log cleared"));
				}
			}
		}
	})
);

// ==================== Lifecycle ====================

void USeinCommandLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SimSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (SimSubsystem.IsValid())
	{
		CommandsDelegateHandle = SimSubsystem->OnCommandsProcessing.AddUObject(
			this, &USeinCommandLogSubsystem::OnCommandsProcessing);
	}
}

void USeinCommandLogSubsystem::Deinitialize()
{
	if (SimSubsystem.IsValid())
	{
		SimSubsystem->OnCommandsProcessing.Remove(CommandsDelegateHandle);
	}
	CommandsDelegateHandle.Reset();
	LogEntries.Empty();

	Super::Deinitialize();
}

// ==================== Command Capture ====================

void USeinCommandLogSubsystem::OnCommandsProcessing(int32 Tick, const TArray<FSeinCommand>& Commands)
{
	for (const FSeinCommand& Cmd : Commands)
	{
		FSeinCommandLogEntry Entry;
		Entry.Tick = Tick;
		Entry.PlayerIndex = static_cast<int32>(Cmd.PlayerID.Value);
		Entry.EntityIndex = Cmd.EntityHandle.IsValid() ? Cmd.EntityHandle.Index : -1;
		Entry.CommandType = Cmd.CommandType;
		Entry.Description = DescribeCommand(Cmd);
		Entry.DisplayColor = GetCommandColor(Cmd.CommandType);

		if (LogEntries.Num() >= MaxLogEntries)
		{
			LogEntries.RemoveAt(0);
		}
		LogEntries.Add(Entry);
	}
}

// ==================== Helpers ====================

FString USeinCommandLogSubsystem::DescribeCommand(const FSeinCommand& Cmd)
{
	switch (Cmd.CommandType)
	{
	case ESeinCommandType::ActivateAbility:
	{
		const FString TagStr = Cmd.AbilityTag.IsValid() ? Cmd.AbilityTag.ToString() : TEXT("None");
		const FVector Loc = Cmd.TargetLocation.ToVector();
		FString Desc = FString::Printf(TEXT("ActivateAbility [%s] @ (%.0f, %.0f, %.0f)"),
			*TagStr, Loc.X, Loc.Y, Loc.Z);
		if (Cmd.TargetEntity.IsValid())
		{
			Desc += FString::Printf(TEXT(" -> E%d"), Cmd.TargetEntity.Index);
		}
		if (Cmd.bQueueCommand)
		{
			Desc += TEXT(" [Q]");
		}
		return Desc;
	}
	case ESeinCommandType::CancelAbility:
		return TEXT("CancelAbility");
	case ESeinCommandType::QueueProduction:
		return FString::Printf(TEXT("QueueProduction [%s]"),
			Cmd.AbilityTag.IsValid() ? *Cmd.AbilityTag.ToString() : TEXT("None"));
	case ESeinCommandType::CancelProduction:
		return FString::Printf(TEXT("CancelProduction idx=%d"), Cmd.QueueIndex);
	case ESeinCommandType::SetRallyPoint:
	{
		const FVector Loc = Cmd.TargetLocation.ToVector();
		return FString::Printf(TEXT("SetRallyPoint (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z);
	}
	case ESeinCommandType::Ping:
	{
		const FVector Loc = Cmd.TargetLocation.ToVector();
		FString Desc = FString::Printf(TEXT("PING (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z);
		if (Cmd.TargetEntity.IsValid())
		{
			Desc += FString::Printf(TEXT(" on E%d"), Cmd.TargetEntity.Index);
		}
		return Desc;
	}
	case ESeinCommandType::CameraUpdate:
	{
		const FVector Loc = Cmd.TargetLocation.ToVector();
		const float Yaw = Cmd.AuxA.ToFloat();
		const float Zoom = Cmd.AuxB.ToFloat();
		const float Pitch = Cmd.AuxLocation.X.ToFloat();
		return FString::Printf(TEXT("CameraUpdate pos=(%.0f,%.0f,%.0f) yaw=%.1f pitch=%.1f zoom=%.0f"),
			Loc.X, Loc.Y, Loc.Z, Yaw, Pitch, Zoom);
	}
	case ESeinCommandType::SelectionChanged:
		return FString::Printf(TEXT("SelectionChanged (%d ents, focus=%d)"),
			Cmd.EntityList.Num(), Cmd.ActiveFocusIndex);
	default:
		return TEXT("???");
	}
}

FColor USeinCommandLogSubsystem::GetCommandColor(ESeinCommandType Type)
{
	switch (Type)
	{
	case ESeinCommandType::ActivateAbility:   return FColor(80, 255, 80);
	case ESeinCommandType::CancelAbility:     return FColor(255, 160, 0);
	case ESeinCommandType::QueueProduction:   return FColor(80, 220, 255);
	case ESeinCommandType::CancelProduction:  return FColor(255, 255, 80);
	case ESeinCommandType::SetRallyPoint:     return FColor(100, 200, 255);
	case ESeinCommandType::Ping:              return FColor(255, 80, 255);
	case ESeinCommandType::CameraUpdate:      return FColor(120, 120, 120);
	case ESeinCommandType::SelectionChanged:  return FColor(120, 120, 120);
	default:                                  return FColor::White;
	}
}
