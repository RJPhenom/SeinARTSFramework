/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadBPFL.cpp
 * @brief   Implementation of squad query Blueprint nodes.
 */

#include "Lib/SeinSquadBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"

USeinWorldSubsystem* USeinSquadBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

TArray<FSeinEntityHandle> USeinSquadBPFL::SeinGetSquadMembers(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return TArray<FSeinEntityHandle>();

	const FSeinSquadComponent* SquadComp = Subsystem->GetComponent<FSeinSquadComponent>(SquadHandle);
	if (!SquadComp) return TArray<FSeinEntityHandle>();

	return SquadComp->Members;
}

FSeinEntityHandle USeinSquadBPFL::SeinGetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadComponent* SquadComp = Subsystem->GetComponent<FSeinSquadComponent>(SquadHandle);
	if (!SquadComp) return FSeinEntityHandle::Invalid();

	return SquadComp->Leader;
}

FSeinEntityHandle USeinSquadBPFL::SeinGetEntitySquad(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadMemberComponent* MemberComp = Subsystem->GetComponent<FSeinSquadMemberComponent>(MemberHandle);
	if (!MemberComp) return FSeinEntityHandle::Invalid();

	return MemberComp->SquadEntity;
}

bool USeinSquadBPFL::SeinIsSquadMember(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	return Subsystem->HasComponent<FSeinSquadMemberComponent>(EntityHandle);
}

int32 USeinSquadBPFL::SeinGetSquadSize(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	const FSeinSquadComponent* SquadComp = Subsystem->GetComponent<FSeinSquadComponent>(SquadHandle);
	if (!SquadComp) return 0;

	return SquadComp->GetAliveCount();
}
