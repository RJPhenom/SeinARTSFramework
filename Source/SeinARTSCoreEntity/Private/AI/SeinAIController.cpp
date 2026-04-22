/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIController.cpp
 */

#include "AI/SeinAIController.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"

DEFINE_LOG_CATEGORY(LogSeinAI);

void USeinAIController::EmitCommand(const FSeinCommand& Command)
{
	if (!WorldSubsystem)
	{
		UE_LOG(LogSeinAI, Warning, TEXT("EmitCommand: AI controller %s has no WorldSubsystem (not registered?)"), *GetName());
		return;
	}
	// The author is expected to stamp PlayerID on the command; we don't
	// silently overwrite because it might legitimately target the Neutral
	// player for e.g. scenario-owner-like emissions.
	WorldSubsystem->EnqueueCommand(Command);
}
