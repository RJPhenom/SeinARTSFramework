/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadBPFL.cpp
 * @brief   Implementation of squad query Blueprint nodes.
 */

#include "Lib/SeinSquadBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinSquadData.h"
#include "Components/SeinSquadMemberData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinSquadBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinSquadBPFL::SeinGetSquadData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData: no SeinWorldSubsystem")); return false; }
	const FSeinSquadData* Data = Subsystem->GetComponent<FSeinSquadData>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData: entity %s invalid or has no FSeinSquadData"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinSquadData> USeinSquadBPFL::SeinGetSquadDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinSquadData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinSquadData* Data = Subsystem->GetComponent<FSeinSquadData>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}

bool USeinSquadBPFL::SeinGetSquadMemberData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadMemberData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData: no SeinWorldSubsystem")); return false; }
	const FSeinSquadMemberData* Data = Subsystem->GetComponent<FSeinSquadMemberData>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData: entity %s invalid or has no FSeinSquadMemberData"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinSquadMemberData> USeinSquadBPFL::SeinGetSquadMemberDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinSquadMemberData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinSquadMemberData* Data = Subsystem->GetComponent<FSeinSquadMemberData>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}

TArray<FSeinEntityHandle> USeinSquadBPFL::SeinGetSquadMembers(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return TArray<FSeinEntityHandle>();

	const FSeinSquadData* SquadComp = Subsystem->GetComponent<FSeinSquadData>(SquadHandle);
	if (!SquadComp) return TArray<FSeinEntityHandle>();

	return SquadComp->Members;
}

FSeinEntityHandle USeinSquadBPFL::SeinGetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadData* SquadComp = Subsystem->GetComponent<FSeinSquadData>(SquadHandle);
	if (!SquadComp) return FSeinEntityHandle::Invalid();

	return SquadComp->Leader;
}

FSeinEntityHandle USeinSquadBPFL::SeinGetEntitySquad(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadMemberData* MemberComp = Subsystem->GetComponent<FSeinSquadMemberData>(MemberHandle);
	if (!MemberComp) return FSeinEntityHandle::Invalid();

	return MemberComp->SquadEntity;
}

bool USeinSquadBPFL::SeinIsSquadMember(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	return Subsystem->HasComponent<FSeinSquadMemberData>(EntityHandle);
}

int32 USeinSquadBPFL::SeinGetSquadSize(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	const FSeinSquadData* SquadComp = Subsystem->GetComponent<FSeinSquadData>(SquadHandle);
	if (!SquadComp) return 0;

	return SquadComp->GetAliveCount();
}
