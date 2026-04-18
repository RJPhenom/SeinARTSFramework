/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityViewModel.cpp
 * @brief   Entity ViewModel implementation.
 */

#include "ViewModel/SeinEntityViewModel.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinTagData.h"
#include "Core/SeinPlayerState.h"
#include "StructUtils/InstancedStruct.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"

void USeinEntityViewModel::Initialize(FSeinEntityHandle InHandle, USeinWorldSubsystem* InWorldSubsystem)
{
	Entity = InHandle;
	WorldSubsystem = InWorldSubsystem;

	if (!InWorldSubsystem || !InHandle.IsValid())
	{
		bIsAlive = false;
		return;
	}

	bIsAlive = InWorldSubsystem->IsEntityAlive(InHandle);
	OwnerPlayerID = InWorldSubsystem->GetEntityOwner(InHandle);

	// Cache identity data from archetype definition (via actor bridge)
	UWorld* World = InWorldSubsystem->GetWorld();
	if (World)
	{
		USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
		if (Bridge)
		{
			ASeinActor* Actor = Bridge->GetActorForEntity(InHandle);
			if (Actor && Actor->ArchetypeDefinition)
			{
				USeinArchetypeDefinition* Archetype = Actor->ArchetypeDefinition;
				DisplayName = Archetype->DisplayName;
				Icon = Archetype->Icon;
				Portrait = Archetype->Portrait;
				ArchetypeTag = Archetype->ArchetypeTag;
			}
		}
	}
}

void USeinEntityViewModel::Refresh()
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return;
	}

	bIsAlive = WorldSubsystem->IsEntityAlive(Entity);
	if (!bIsAlive)
	{
		Invalidate();
		return;
	}

	OwnerPlayerID = WorldSubsystem->GetEntityOwner(Entity);

	OnRefreshed.Broadcast();
}

void USeinEntityViewModel::Invalidate()
{
	bIsAlive = false;
	OnInvalidated.Broadcast();
}

// ==================== Generic Data Access ====================

float USeinEntityViewModel::GetResolvedAttribute(UScriptStruct* ComponentType, FName FieldName) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return 0.0f;
	}

	FFixedPoint Resolved = WorldSubsystem->ResolveAttribute(Entity, ComponentType, FieldName);
	return Resolved.ToFloat();
}

float USeinEntityViewModel::GetBaseAttribute(UScriptStruct* ComponentType, FName FieldName) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return 0.0f;
	}

	ISeinComponentStorage* Storage = WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return 0.0f;
	}

	const void* CompData = Storage->GetComponentRaw(Entity);
	if (!CompData)
	{
		return 0.0f;
	}

	// Find the FFixedPoint field via reflection
	FProperty* Prop = ComponentType->FindPropertyByName(FieldName);
	if (!Prop)
	{
		return 0.0f;
	}

	const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (StructProp && StructProp->Struct == FFixedPoint::StaticStruct())
	{
		const FFixedPoint* Value = StructProp->ContainerPtrToValuePtr<FFixedPoint>(CompData);
		return Value ? Value->ToFloat() : 0.0f;
	}

	return 0.0f;
}

bool USeinEntityViewModel::HasComponent(UScriptStruct* ComponentType) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return false;
	}

	const ISeinComponentStorage* Storage = WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return false;
	}

	return Storage->HasComponent(Entity);
}

FInstancedStruct USeinEntityViewModel::GetComponentData(UScriptStruct* ComponentType) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return FInstancedStruct();
	}

	const ISeinComponentStorage* Storage = WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return FInstancedStruct();
	}

	const void* CompData = Storage->GetComponentRaw(Entity);
	if (!CompData)
	{
		return FInstancedStruct();
	}

	FInstancedStruct Result;
	Result.InitializeAs(ComponentType, static_cast<const uint8*>(CompData));
	return Result;
}

FGameplayTagContainer USeinEntityViewModel::GetTags() const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return FGameplayTagContainer();
	}

	const FSeinTagData* TagComp = WorldSubsystem->GetComponent<FSeinTagData>(Entity);
	if (!TagComp)
	{
		return FGameplayTagContainer();
	}

	return TagComp->CombinedTags;
}

// ==================== Relationship ====================

ESeinRelation USeinEntityViewModel::GetRelationToPlayer(FSeinPlayerID PlayerID) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return ESeinRelation::Neutral;
	}

	if (OwnerPlayerID.IsNeutral())
	{
		return ESeinRelation::Neutral;
	}

	if (OwnerPlayerID == PlayerID)
	{
		return ESeinRelation::Friendly;
	}

	// Check team alliance
	const FSeinPlayerState* OwnerState = WorldSubsystem->GetPlayerState(OwnerPlayerID);
	const FSeinPlayerState* OtherState = WorldSubsystem->GetPlayerState(PlayerID);

	if (OwnerState && OtherState && OwnerState->TeamID != 0 && OwnerState->TeamID == OtherState->TeamID)
	{
		return ESeinRelation::Friendly;
	}

	return ESeinRelation::Enemy;
}

ESeinRelation USeinEntityViewModel::GetRelationToLocalPlayer() const
{
	UWorld* World = WorldSubsystem.IsValid() ? WorldSubsystem->GetWorld() : nullptr;
	if (!World)
	{
		return ESeinRelation::Neutral;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(World->GetFirstPlayerController());
	if (!PC)
	{
		return ESeinRelation::Neutral;
	}

	return GetRelationToPlayer(PC->SeinPlayerID);
}

// ==================== Ability Access ====================

TArray<FSeinAbilityInfo> USeinEntityViewModel::GetAbilities() const
{
	TArray<FSeinAbilityInfo> Result;

	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return Result;
	}

	const FSeinAbilityData* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityData>(Entity);
	if (!AbilityComp)
	{
		return Result;
	}

	for (const TObjectPtr<USeinAbility>& Ability : AbilityComp->AbilityInstances)
	{
		if (Ability)
		{
			Result.Add(BuildAbilityInfo(Ability));
		}
	}

	return Result;
}

FSeinAbilityInfo USeinEntityViewModel::GetAbilityByTag(FGameplayTag Tag) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return FSeinAbilityInfo();
	}

	const FSeinAbilityData* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityData>(Entity);
	if (!AbilityComp)
	{
		return FSeinAbilityInfo();
	}

	USeinAbility* Found = AbilityComp->FindAbilityByTag(Tag);
	if (!Found)
	{
		return FSeinAbilityInfo();
	}

	return BuildAbilityInfo(Found);
}

bool USeinEntityViewModel::HasAbilityWithTag(FGameplayTag Tag) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return false;
	}

	const FSeinAbilityData* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityData>(Entity);
	return AbilityComp && AbilityComp->HasAbilityWithTag(Tag);
}

FSeinAbilityInfo USeinEntityViewModel::BuildAbilityInfo(const USeinAbility* Ability) const
{
	FSeinAbilityInfo Info;
	if (!Ability)
	{
		return Info;
	}

	Info.Name = Ability->AbilityName;
	Info.AbilityTag = Ability->AbilityTag;
	Info.TargetType = Ability->TargetType;
	Info.Cooldown = Ability->Cooldown.ToFloat();
	Info.CooldownRemaining = Ability->CooldownRemaining.ToFloat();
	Info.bIsActive = Ability->bIsActive;
	Info.bIsPassive = Ability->bIsPassive;
	Info.bIsOnCooldown = Ability->IsOnCooldown();
	Info.QueryTags = Ability->QueryTags;

	// Convert resource cost from FFixedPoint to float
	for (const auto& Pair : Ability->ResourceCost)
	{
		Info.ResourceCost.Add(Pair.Key, Pair.Value.ToFloat());
	}

	return Info;
}
