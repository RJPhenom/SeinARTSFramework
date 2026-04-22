/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDiplomacyBPFL.cpp
 */

#include "Lib/SeinDiplomacyBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"

USeinWorldSubsystem* USeinDiplomacyBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FGameplayTagContainer USeinDiplomacyBPFL::SeinGetDiplomacyTags(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetDiplomacyTags(FromPlayer, ToPlayer) : FGameplayTagContainer{};
}

bool USeinDiplomacyBPFL::SeinHasDiplomacyTag(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, FGameplayTag Tag, bool bBidirectional)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub || !Tag.IsValid()) return false;
	const bool bForward = Sub->GetDiplomacyTags(FromPlayer, ToPlayer).HasTagExact(Tag);
	if (!bBidirectional) return bForward;
	const bool bReverse = Sub->GetDiplomacyTags(ToPlayer, FromPlayer).HasTagExact(Tag);
	return bForward && bReverse;
}

TArray<FSeinDiplomacyKey> USeinDiplomacyBPFL::SeinGetPairsWithTag(const UObject* WorldContextObject, FGameplayTag Tag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetPairsWithDiplomacyTag(Tag) : TArray<FSeinDiplomacyKey>{};
}

bool USeinDiplomacyBPFL::SeinAreAllied(const UObject* WorldContextObject, FSeinPlayerID A, FSeinPlayerID B)
{
	return SeinHasDiplomacyTag(WorldContextObject, A, B, SeinARTSTags::Diplomacy_Permission_Allied, /*bBidirectional=*/true);
}

bool USeinDiplomacyBPFL::SeinAreAtWar(const UObject* WorldContextObject, FSeinPlayerID A, FSeinPlayerID B)
{
	// AtWar is commonly mutual by convention, but the framework stores it
	// directionally; either side set is sufficient to trigger default-hostile
	// targeting. Designers who need strict mutual-war semantics check both.
	return SeinHasDiplomacyTag(WorldContextObject, A, B, SeinARTSTags::Diplomacy_State_AtWar, /*bBidirectional=*/false)
		|| SeinHasDiplomacyTag(WorldContextObject, B, A, SeinARTSTags::Diplomacy_State_AtWar, /*bBidirectional=*/false);
}

bool USeinDiplomacyBPFL::SeinHasSharedVision(const UObject* WorldContextObject, FSeinPlayerID Viewer, FSeinPlayerID Target)
{
	// Viewer has shared vision with Target iff Target grants `Permission.SharedVision` toward Viewer.
	return SeinHasDiplomacyTag(WorldContextObject, Target, Viewer, SeinARTSTags::Diplomacy_Permission_SharedVision, false);
}

void USeinDiplomacyBPFL::SeinModifyDiplomacy(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinDiplomacyCommandPayload Pay;
	Pay.FromPlayer = FromPlayer;
	Pay.ToPlayer = ToPlayer;
	Pay.TagsToAdd = TagsToAdd;
	Pay.TagsToRemove = TagsToRemove;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_ModifyDiplomacy;
	Cmd.PlayerID = FromPlayer;
	Cmd.Payload.InitializeAs<FSeinDiplomacyCommandPayload>(Pay);
	Sub->EnqueueCommand(Cmd);
}

void USeinDiplomacyBPFL::SeinDeclareWar(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer)
{
	FGameplayTagContainer Add; Add.AddTag(SeinARTSTags::Diplomacy_State_AtWar);
	FGameplayTagContainer Remove;
	Remove.AddTag(SeinARTSTags::Diplomacy_State_Peace);
	Remove.AddTag(SeinARTSTags::Diplomacy_State_Truce);
	SeinModifyDiplomacy(WorldContextObject, FromPlayer, ToPlayer, Add, Remove);
}

void USeinDiplomacyBPFL::SeinDeclarePeace(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer)
{
	FGameplayTagContainer Add; Add.AddTag(SeinARTSTags::Diplomacy_State_Peace);
	FGameplayTagContainer Remove; Remove.AddTag(SeinARTSTags::Diplomacy_State_AtWar);
	SeinModifyDiplomacy(WorldContextObject, FromPlayer, ToPlayer, Add, Remove);
}

void USeinDiplomacyBPFL::SeinProposeTruce(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer)
{
	FGameplayTagContainer Add; Add.AddTag(SeinARTSTags::Diplomacy_State_Truce);
	FGameplayTagContainer Remove;
	SeinModifyDiplomacy(WorldContextObject, FromPlayer, ToPlayer, Add, Remove);
}
