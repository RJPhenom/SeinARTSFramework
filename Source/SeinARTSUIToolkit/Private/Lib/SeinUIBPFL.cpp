/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUIBPFL.cpp
 * @brief   UI Blueprint Function Library implementation.
 */

#include "Lib/SeinUIBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinProductionData.h"
#include "Core/SeinPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// ==================== Internal Helpers ====================

namespace
{
	USeinArchetypeDefinition* GetArchetypeForEntity(const UObject* WorldContextObject, FSeinEntityHandle Handle)
	{
		if (!Handle.IsValid() || !WorldContextObject)
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World)
		{
			return nullptr;
		}

		USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
		if (!Bridge)
		{
			return nullptr;
		}

		ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
		return Actor ? Actor->ArchetypeDefinition : nullptr;
	}

	USeinWorldSubsystem* GetSimSubsystem(const UObject* WorldContextObject)
	{
		if (!WorldContextObject)
		{
			return nullptr;
		}
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}
}

// ==================== Entity Display Helpers ====================

FText USeinUIBPFL::SeinGetEntityDisplayName(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	USeinArchetypeDefinition* Archetype = GetArchetypeForEntity(WorldContextObject, Handle);
	return Archetype ? Archetype->DisplayName : FText::GetEmpty();
}

UTexture2D* USeinUIBPFL::SeinGetEntityIcon(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	USeinArchetypeDefinition* Archetype = GetArchetypeForEntity(WorldContextObject, Handle);
	return Archetype ? Archetype->Icon.Get() : nullptr;
}

UTexture2D* USeinUIBPFL::SeinGetEntityPortrait(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	USeinArchetypeDefinition* Archetype = GetArchetypeForEntity(WorldContextObject, Handle);
	return Archetype ? Archetype->Portrait.Get() : nullptr;
}

FGameplayTag USeinUIBPFL::SeinGetEntityArchetypeTag(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	USeinArchetypeDefinition* Archetype = GetArchetypeForEntity(WorldContextObject, Handle);
	return Archetype ? Archetype->ArchetypeTag : FGameplayTag();
}

ESeinRelation USeinUIBPFL::SeinGetEntityRelation(const UObject* WorldContextObject, FSeinEntityHandle Handle, FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Handle.IsValid())
	{
		return ESeinRelation::Neutral;
	}

	FSeinPlayerID Owner = SimSub->GetEntityOwner(Handle);

	if (Owner.IsNeutral())
	{
		return ESeinRelation::Neutral;
	}

	if (Owner == PlayerID)
	{
		return ESeinRelation::Friendly;
	}

	// Check team alliance
	const FSeinPlayerState* OwnerState = SimSub->GetPlayerState(Owner);
	const FSeinPlayerState* OtherState = SimSub->GetPlayerState(PlayerID);

	if (OwnerState && OtherState && OwnerState->TeamID != 0 && OwnerState->TeamID == OtherState->TeamID)
	{
		return ESeinRelation::Friendly;
	}

	return ESeinRelation::Enemy;
}

// ==================== Conversion Helpers ====================

float USeinUIBPFL::SeinFixedToFloat(FFixedPoint Value)
{
	return Value.ToFloat();
}

FVector USeinUIBPFL::SeinFixedVectorToVector(const FFixedVector& Value)
{
	return Value.ToVector();
}

FText USeinUIBPFL::SeinFormatResourceCost(const TMap<FName, float>& Cost)
{
	if (Cost.IsEmpty())
	{
		return FText::FromString(TEXT("Free"));
	}

	FString Result;
	bool bFirst = true;

	for (const auto& Pair : Cost)
	{
		if (!bFirst)
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("%d %s"), FMath::RoundToInt(Pair.Value), *Pair.Key.ToString());
		bFirst = false;
	}

	return FText::FromString(Result);
}

// ==================== Screen Projection ====================

bool USeinUIBPFL::SeinWorldToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FVector WorldPos, FVector2D& OutScreenPos)
{
	if (!PlayerController)
	{
		OutScreenPos = FVector2D::ZeroVector;
		return false;
	}

	return PlayerController->ProjectWorldLocationToScreen(WorldPos, OutScreenPos, true);
}

bool USeinUIBPFL::SeinScreenToWorld(const UObject* WorldContextObject, APlayerController* PlayerController, FVector2D ScreenPos, float GroundZ, FVector& OutWorldPos)
{
	if (!PlayerController)
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	FVector WorldOrigin, WorldDirection;
	if (!PlayerController->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, WorldOrigin, WorldDirection))
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	// Intersect ray with horizontal plane at GroundZ
	if (FMath::IsNearlyZero(WorldDirection.Z))
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	const float T = (GroundZ - WorldOrigin.Z) / WorldDirection.Z;
	if (T < 0.0f)
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	OutWorldPos = WorldOrigin + WorldDirection * T;
	return true;
}

// ==================== Minimap Math ====================

FVector2D USeinUIBPFL::SeinWorldToMinimap(FVector WorldPos, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax)
{
	const FVector2D Range = WorldBoundsMax - WorldBoundsMin;
	if (Range.X <= 0.0f || Range.Y <= 0.0f)
	{
		return FVector2D(0.5f, 0.5f);
	}

	return FVector2D(
		(WorldPos.X - WorldBoundsMin.X) / Range.X,
		(WorldPos.Y - WorldBoundsMin.Y) / Range.Y
	);
}

FVector USeinUIBPFL::SeinMinimapToWorld(FVector2D MinimapUV, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ)
{
	const FVector2D Range = WorldBoundsMax - WorldBoundsMin;
	return FVector(
		WorldBoundsMin.X + MinimapUV.X * Range.X,
		WorldBoundsMin.Y + MinimapUV.Y * Range.Y,
		GroundZ
	);
}

TArray<FVector2D> USeinUIBPFL::SeinGetCameraFrustumCorners(APlayerController* PlayerController, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ)
{
	TArray<FVector2D> Corners;

	if (!PlayerController)
	{
		return Corners;
	}

	// Get viewport size
	int32 ViewportX, ViewportY;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return Corners;
	}

	// Screen corners: top-left, top-right, bottom-right, bottom-left
	const FVector2D ScreenCorners[4] = {
		FVector2D(0.0f, 0.0f),
		FVector2D(static_cast<float>(ViewportX), 0.0f),
		FVector2D(static_cast<float>(ViewportX), static_cast<float>(ViewportY)),
		FVector2D(0.0f, static_cast<float>(ViewportY))
	};

	Corners.Reserve(4);
	for (int32 i = 0; i < 4; ++i)
	{
		FVector WorldPos;
		if (SeinScreenToWorld(PlayerController, PlayerController, ScreenCorners[i], GroundZ, WorldPos))
		{
			Corners.Add(SeinWorldToMinimap(WorldPos, WorldBoundsMin, WorldBoundsMax));
		}
	}

	return Corners;
}

// ==================== Action Slot Builders ====================

FSeinActionSlotData USeinUIBPFL::SeinBuildAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FGameplayTag AbilityTag, int32 SlotIndex)
{
	FSeinActionSlotData Data;
	Data.SlotIndex = SlotIndex;
	Data.ActionTag = AbilityTag;
	Data.State = ESeinActionSlotState::Empty;

	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Entity.IsValid())
	{
		return Data;
	}

	const FSeinAbilityData* AbilityComp = SimSub->GetComponent<FSeinAbilityData>(Entity);
	if (!AbilityComp)
	{
		return Data;
	}

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(AbilityTag);
	if (!Ability)
	{
		return Data;
	}

	Data.Name = Ability->AbilityName;
	Data.ActionTag = Ability->AbilityTag;

	// Convert resource cost to float
	for (const auto& Pair : Ability->ResourceCost)
	{
		Data.ResourceCost.Add(Pair.Key, Pair.Value.ToFloat());
	}

	// Determine state
	if (Ability->bIsActive)
	{
		Data.State = ESeinActionSlotState::Active;
	}
	else if (Ability->IsOnCooldown())
	{
		Data.State = ESeinActionSlotState::OnCooldown;
		const float TotalCD = Ability->Cooldown.ToFloat();
		Data.CooldownPercent = TotalCD > 0.0f ? Ability->CooldownRemaining.ToFloat() / TotalCD : 0.0f;
	}
	else
	{
		Data.State = ESeinActionSlotState::Available;
	}

	return Data;
}

TArray<FSeinActionSlotData> USeinUIBPFL::SeinBuildAllAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	TArray<FSeinActionSlotData> Result;

	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Entity.IsValid())
	{
		return Result;
	}

	const FSeinAbilityData* AbilityComp = SimSub->GetComponent<FSeinAbilityData>(Entity);
	if (!AbilityComp)
	{
		return Result;
	}

	int32 SlotIndex = 0;
	for (const TObjectPtr<USeinAbility>& Ability : AbilityComp->AbilityInstances)
	{
		if (Ability && !Ability->bIsPassive)
		{
			Result.Add(SeinBuildAbilitySlotData(WorldContextObject, Entity, Ability->AbilityTag, SlotIndex));
			++SlotIndex;
		}
	}

	return Result;
}

TArray<FSeinActionSlotData> USeinUIBPFL::SeinBuildProductionSlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinPlayerID PlayerID)
{
	TArray<FSeinActionSlotData> Result;

	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Entity.IsValid())
	{
		return Result;
	}

	const FSeinProductionData* ProdComp = SimSub->GetComponent<FSeinProductionData>(Entity);
	if (!ProdComp)
	{
		return Result;
	}

	const FSeinPlayerState* PlayerState = SimSub->GetPlayerState(PlayerID);

	int32 SlotIndex = 0;
	for (const TSubclassOf<ASeinActor>& ActorClass : ProdComp->ProducibleClasses)
	{
		FSeinActionSlotData Data;
		Data.SlotIndex = SlotIndex;
		Data.State = ESeinActionSlotState::Empty;

		if (ActorClass)
		{
			ASeinActor* CDO = ActorClass.GetDefaultObject();
			if (CDO && CDO->ArchetypeDefinition)
			{
				USeinArchetypeDefinition* Archetype = CDO->ArchetypeDefinition;
				Data.Name = Archetype->DisplayName;
				Data.Tooltip = Archetype->Description;
				Data.Icon = Archetype->Icon;
				Data.ActionTag = Archetype->ArchetypeTag;

				// Convert cost to float
				for (const auto& Pair : Archetype->ProductionCost)
				{
					Data.ResourceCost.Add(Pair.Key, Pair.Value.ToFloat());
				}

				// Determine state
				bool bPrereqsMet = !PlayerState || PlayerState->HasAllTechTags(Archetype->PrerequisiteTags);
				bool bCanAfford = !PlayerState || PlayerState->CanAfford(Archetype->ProductionCost);
				bool bQueueFull = !ProdComp->CanQueueMore();

				if (!bPrereqsMet)
				{
					Data.State = ESeinActionSlotState::Disabled;
				}
				else if (!bCanAfford)
				{
					Data.State = ESeinActionSlotState::Unaffordable;
				}
				else if (bQueueFull)
				{
					Data.State = ESeinActionSlotState::Disabled;
				}
				else
				{
					Data.State = ESeinActionSlotState::Available;
				}
			}
		}

		Result.Add(Data);
		++SlotIndex;
	}

	return Result;
}

// ==================== World-Space Widget Helpers ====================

bool USeinUIBPFL::SeinProjectEntityToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float VerticalWorldOffset, FVector2D& OutScreenPos)
{
	OutScreenPos = FVector2D::ZeroVector;

	if (!PlayerController || !Handle.IsValid())
	{
		return false;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return false;
	}

	USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Bridge)
	{
		return false;
	}

	ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
	if (!Actor)
	{
		return false;
	}

	FVector WorldPos = Actor->GetActorLocation() + FVector(0.0f, 0.0f, VerticalWorldOffset);
	return PlayerController->ProjectWorldLocationToScreen(WorldPos, OutScreenPos, true);
}

bool USeinUIBPFL::SeinIsEntityOnScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float ScreenMargin)
{
	FVector2D ScreenPos;
	if (!SeinProjectEntityToScreen(WorldContextObject, PlayerController, Handle, 0.0f, ScreenPos))
	{
		return false;
	}

	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportX, ViewportY;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	return ScreenPos.X >= -ScreenMargin
		&& ScreenPos.Y >= -ScreenMargin
		&& ScreenPos.X <= ViewportX + ScreenMargin
		&& ScreenPos.Y <= ViewportY + ScreenMargin;
}
