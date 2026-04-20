/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceIncomeBPFL.cpp
 */

#include "Lib/SeinResourceIncomeBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinResourceIncomeBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinResourceIncomeBPFL::SeinGetResourceIncomeData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinResourceIncomeData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetResourceIncomeData: no SeinWorldSubsystem")); return false; }
	const FSeinResourceIncomeData* Data = Subsystem->GetComponent<FSeinResourceIncomeData>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetResourceIncomeData: entity %s invalid or has no FSeinResourceIncomeData"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinResourceIncomeData> USeinResourceIncomeBPFL::SeinGetResourceIncomeDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinResourceIncomeData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinResourceIncomeData* Data = Subsystem->GetComponent<FSeinResourceIncomeData>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetResourceIncomeData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}
