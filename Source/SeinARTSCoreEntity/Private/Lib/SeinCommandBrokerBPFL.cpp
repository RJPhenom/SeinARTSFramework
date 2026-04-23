/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerBPFL.cpp
 */

#include "Lib/SeinCommandBrokerBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBroker, Log, All);

USeinWorldSubsystem* USeinCommandBrokerBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinCommandBrokerBPFL::SeinGetBrokerData(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle, FSeinCommandBrokerData& OutData)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("GetBrokerData: handle %s is not a broker"), *BrokerHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

FSeinEntityHandle USeinCommandBrokerBPFL::SeinGetCurrentBroker(const UObject* WorldContextObject, FSeinEntityHandle Member)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FSeinEntityHandle::Invalid();
	const FSeinBrokerMembershipData* Memb = Sub->GetComponent<FSeinBrokerMembershipData>(Member);
	if (!Memb) return FSeinEntityHandle::Invalid();
	return Memb->CurrentBrokerHandle;
}

TArray<FSeinEntityHandle> USeinCommandBrokerBPFL::SeinGetBrokerMembers(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return {};
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->Members : TArray<FSeinEntityHandle>{};
}

FFixedVector USeinCommandBrokerBPFL::SeinGetBrokerCentroid(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FFixedVector();
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->Centroid : FFixedVector();
}

FGameplayTagContainer USeinCommandBrokerBPFL::SeinGetBrokerActiveOrderContext(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FGameplayTagContainer();
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->CurrentOrderContext : FGameplayTagContainer();
}

int32 USeinCommandBrokerBPFL::SeinGetBrokerQueueLength(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return 0;
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->OrderQueue.Num() : 0;
}

void USeinCommandBrokerBPFL::SeinIssueBrokerOrder(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	const TArray<FSeinEntityHandle>& Members,
	const FGameplayTagContainer& CommandContext,
	FSeinEntityHandle TargetEntity,
	FFixedVector TargetLocation,
	bool bQueueCommand,
	FFixedVector FormationEnd)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("IssueBrokerOrder: no world subsystem"));
		return;
	}
	if (Members.Num() == 0)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("IssueBrokerOrder: empty member list"));
		return;
	}

	FSeinBrokerOrderPayload Payload;
	Payload.CommandContext = CommandContext;
	Payload.FormationEnd = FormationEnd;

	FSeinCommand Cmd;
	Cmd.PlayerID = PlayerID;
	Cmd.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
	Cmd.TargetEntity = TargetEntity;
	Cmd.TargetLocation = TargetLocation;
	Cmd.EntityList = Members;
	Cmd.bQueueCommand = bQueueCommand;
	Cmd.Payload = FInstancedStruct::Make(Payload);

	Sub->EnqueueCommand(Cmd);
}
