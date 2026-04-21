/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionBPFL.cpp
 * @brief   Production availability queries for UI binding.
 */

#include "Lib/SeinProductionBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinProductionData.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Effects/SeinEffect.h"
#include "Actor/SeinActor.h"
#include "Core/SeinPlayerState.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinProductionBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinProductionBPFL::SeinGetProductionData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinProductionData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetProductionData: no SeinWorldSubsystem"));
		return false;
	}
	const FSeinProductionData* Data = Subsystem->GetComponent<FSeinProductionData>(EntityHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetProductionData: entity %s invalid or has no FSeinProductionData"), *EntityHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

TArray<FSeinProductionData> USeinProductionBPFL::SeinGetProductionDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinProductionData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinProductionData* Data = Subsystem->GetComponent<FSeinProductionData>(Handle))
		{
			Result.Add(*Data);
		}
		else
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("GetProductionData (batch): skipping entity %s"), *Handle.ToString());
		}
	}
	return Result;
}

TArray<FSeinProductionAvailability> USeinProductionBPFL::SeinGetProductionAvailability(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	FSeinEntityHandle BuildingEntity)
{
	TArray<FSeinProductionAvailability> Result;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	const FSeinProductionData* ProdComp = Subsystem->GetComponent<FSeinProductionData>(BuildingEntity);
	if (!ProdComp) return Result;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	if (!PlayerState) return Result;

	const bool bQueueFull = !ProdComp->CanQueueMore();

	for (const TSubclassOf<ASeinActor>& ProdClass : ProdComp->ProducibleClasses)
	{
		if (!ProdClass) continue;

		const ASeinActor* CDO = GetDefault<ASeinActor>(ProdClass);
		if (!CDO || !CDO->ArchetypeDefinition) continue;

		const USeinArchetypeDefinition* Archetype = CDO->ArchetypeDefinition;

		FSeinProductionAvailability Avail;
		Avail.ActorClass = ProdClass;
		Avail.DisplayName = Archetype->DisplayName;
		Avail.Icon = Archetype->Icon;
		Avail.Cost = Archetype->ProductionCost;
		Avail.BuildTime = Archetype->BuildTime;
		Avail.ArchetypeTag = Archetype->ArchetypeTag;
		Avail.bIsResearch = Archetype->bIsResearch;
		Avail.bPrerequisitesMet = PlayerState->HasAllPlayerTags(Archetype->PrerequisiteTags);
		Avail.bCanAfford = PlayerState->CanAfford(Archetype->ProductionCost);
		Avail.bQueueFull = bQueueFull;

		// Already-researched check: inspect the granted effect's CDO EffectTag
		// against the player's refcounted tag set (DESIGN §10 unification).
		if (Archetype->bIsResearch && Archetype->GrantedTechEffect)
		{
			if (const USeinEffect* EffectDef = GetDefault<USeinEffect>(Archetype->GrantedTechEffect))
			{
				if (EffectDef->EffectTag.IsValid())
				{
					Avail.bAlreadyResearched = PlayerState->HasPlayerTag(EffectDef->EffectTag);
				}
			}
		}

		Result.Add(MoveTemp(Avail));
	}

	return Result;
}

bool USeinProductionBPFL::SeinCanPlayerProduce(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	TSubclassOf<ASeinActor> ActorClass)
{
	if (!ActorClass) return false;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
	if (!CDO || !CDO->ArchetypeDefinition) return false;

	const USeinArchetypeDefinition* Archetype = CDO->ArchetypeDefinition;
	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	if (!PlayerState) return false;

	if (!PlayerState->HasAllPlayerTags(Archetype->PrerequisiteTags)) return false;
	if (!PlayerState->CanAfford(Archetype->ProductionCost)) return false;

	// If research, check not already completed — via the granted effect's EffectTag.
	if (Archetype->bIsResearch && Archetype->GrantedTechEffect)
	{
		if (const USeinEffect* EffectDef = GetDefault<USeinEffect>(Archetype->GrantedTechEffect))
		{
			if (EffectDef->EffectTag.IsValid() && PlayerState->HasPlayerTag(EffectDef->EffectTag))
			{
				return false;
			}
		}
	}

	return true;
}

bool USeinProductionBPFL::SeinPlayerHasTechTag(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	FGameplayTag TechTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState && PlayerState->HasPlayerTag(TechTag);
}

FGameplayTagContainer USeinProductionBPFL::SeinGetPlayerTechTags(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FGameplayTagContainer();

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState ? PlayerState->PlayerTags : FGameplayTagContainer();
}
