/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatBPFL.cpp
 * @brief   Implementation of combat and spatial query Blueprint nodes.
 */

#include "Lib/SeinCombatBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinEntityPool.h"
#include "Components/SeinTagData.h"
#include "Events/SeinVisualEvent.h"
#include "Math/MathLib.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinCombatBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinCombatBPFL::SeinGetCombatData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinCombatData& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetCombatData: no SeinWorldSubsystem in this world context"));
		return false;
	}
	const FSeinCombatData* Data = Subsystem->GetComponent<FSeinCombatData>(EntityHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetCombatData: entity %s invalid or has no FSeinCombatData"), *EntityHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

TArray<FSeinCombatData> USeinCombatBPFL::SeinGetCombatDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinCombatData> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinCombatData* Data = Subsystem->GetComponent<FSeinCombatData>(Handle))
		{
			Result.Add(*Data);
		}
		else
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("GetCombatData (batch): skipping entity %s (invalid or no FSeinCombatData)"), *Handle.ToString());
		}
	}
	return Result;
}

void USeinCombatBPFL::SeinApplyDamage(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Damage, FSeinEntityHandle SourceHandle, FGameplayTag DamageTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Target = Subsystem->GetEntity(TargetHandle);
	if (!Target || !Target->IsAlive()) return;

	Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeDamageEvent(TargetHandle, SourceHandle, Damage));
}

void USeinCombatBPFL::SeinApplyHealing(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Target = Subsystem->GetEntity(TargetHandle);
	if (!Target || !Target->IsAlive()) return;

	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::Healed;
	Event.PrimaryEntity = TargetHandle;
	Event.SecondaryEntity = SourceHandle;
	Event.Value = Amount;
	Subsystem->EnqueueVisualEvent(Event);
}

TArray<FSeinEntityHandle> USeinCombatBPFL::SeinGetEntitiesInRange(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags)
{
	TArray<FSeinEntityHandle> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	const FFixedPoint RadiusSq = Radius * Radius;
	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();

	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		FFixedVector Delta = Entity.Transform.GetLocation() - Origin;
		FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);
		if (DistSq <= RadiusSq)
		{
			if (FilterTags.IsEmpty())
			{
				Result.Add(Handle);
			}
			else
			{
				const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(Handle);
				if (TagComp && TagComp->HasAnyTag(FilterTags))
				{
					Result.Add(Handle);
				}
			}
		}
	});

	return Result;
}

FSeinEntityHandle USeinCombatBPFL::SeinGetNearestEntity(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FFixedPoint RadiusSq = Radius * Radius;
	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();
	FSeinEntityHandle NearestHandle = FSeinEntityHandle::Invalid();
	FFixedPoint NearestDistSq = RadiusSq + FFixedPoint::One;

	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		FFixedVector Delta = Entity.Transform.GetLocation() - Origin;
		FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);
		if (DistSq <= RadiusSq && DistSq < NearestDistSq)
		{
			if (FilterTags.IsEmpty())
			{
				NearestHandle = Handle;
				NearestDistSq = DistSq;
			}
			else
			{
				const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(Handle);
				if (TagComp && TagComp->HasAnyTag(FilterTags))
				{
					NearestHandle = Handle;
					NearestDistSq = DistSq;
				}
			}
		}
	});

	return NearestHandle;
}

TArray<FSeinEntityHandle> USeinCombatBPFL::SeinGetEntitiesInBox(const UObject* WorldContextObject, FFixedVector Min, FFixedVector Max, FGameplayTagContainer FilterTags)
{
	TArray<FSeinEntityHandle> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();

	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		FFixedVector Loc = Entity.Transform.GetLocation();
		if (Loc.X >= Min.X && Loc.X <= Max.X &&
			Loc.Y >= Min.Y && Loc.Y <= Max.Y &&
			Loc.Z >= Min.Z && Loc.Z <= Max.Z)
		{
			if (FilterTags.IsEmpty())
			{
				Result.Add(Handle);
			}
			else
			{
				const FSeinTagData* TagComp = Subsystem->GetComponent<FSeinTagData>(Handle);
				if (TagComp && TagComp->HasAnyTag(FilterTags))
				{
					Result.Add(Handle);
				}
			}
		}
	});

	return Result;
}

FFixedPoint USeinCombatBPFL::SeinGetDistanceBetween(const UObject* WorldContextObject, FSeinEntityHandle EntityA, FSeinEntityHandle EntityB)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinEntity* A = Subsystem->GetEntity(EntityA);
	const FSeinEntity* B = Subsystem->GetEntity(EntityB);
	if (!A || !B) return FFixedPoint::Zero;

	FFixedVector Delta = B->Transform.GetLocation() - A->Transform.GetLocation();
	return Delta.Size();
}

FFixedVector USeinCombatBPFL::SeinGetDirectionTo(const UObject* WorldContextObject, FSeinEntityHandle FromEntity, FSeinEntityHandle ToEntity)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedVector::ZeroVector;

	const FSeinEntity* From = Subsystem->GetEntity(FromEntity);
	const FSeinEntity* To = Subsystem->GetEntity(ToEntity);
	if (!From || !To) return FFixedVector::ZeroVector;

	FFixedVector Delta = To->Transform.GetLocation() - From->Transform.GetLocation();
	FFixedPoint Len = Delta.Size();
	if (Len > FFixedPoint::Zero)
	{
		return Delta / Len;
	}
	return FFixedVector::ZeroVector;
}
