/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementBPFL.cpp
 */

#include "Lib/SeinMovementBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinMovementBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinMovementBPFL::SeinGetMovementData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinMovementData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetMovementData: no SeinWorldSubsystem")); return false; }
	const FSeinMovementData* Data = Subsystem->GetComponent<FSeinMovementData>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetMovementData: entity %s invalid or has no FSeinMovementData"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinMovementData> USeinMovementBPFL::SeinGetMovementDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinMovementData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinMovementData* Data = Subsystem->GetComponent<FSeinMovementData>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetMovementData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}
