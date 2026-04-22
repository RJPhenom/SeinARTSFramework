/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatBPFL.cpp
 * @brief   Thin combat BPFL — writes Health delta, fires visual events, bumps
 *          attribution stats, triggers the death-handler flow. All damage math
 *          (armor, types, accuracy) is the caller's job per DESIGN §11.
 */

#include "Lib/SeinCombatBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinEntityPool.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinTagData.h"
#include "Abilities/SeinAbility.h"
#include "Attributes/SeinAttributeResolver.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinStatsBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinCombatBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

// =====================================================================
// Reads
// =====================================================================

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

// =====================================================================
// Damage / Heal
// =====================================================================

void USeinCombatBPFL::SeinApplyDamage(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FGameplayTag DamageType, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Target = Subsystem->GetEntity(TargetHandle);
	if (!Target || !Target->IsAlive()) return;

	FSeinCombatData* Combat = Subsystem->GetComponent<FSeinCombatData>(TargetHandle);
	if (!Combat) return;

	// Friendly-fire filter (DESIGN §11 / §18). When `bFriendlyFire` is false in
	// the active match settings, damage between players sharing
	// `Diplomacy.Permission.Allied` is suppressed. Designer ability code that
	// wants accidental-fire mechanics (e.g., errant artillery) routes through
	// `SeinMutateAttribute` to bypass this gate.
	const FSeinPlayerID TargetOwner = Subsystem->GetEntityOwner(TargetHandle);
	const FSeinPlayerID SourceOwner = Subsystem->GetEntityOwner(SourceHandle);
	if (!Subsystem->GetMatchSettings().bFriendlyFire &&
		SourceOwner.IsValid() && TargetOwner.IsValid() && SourceOwner != TargetOwner)
	{
		const bool bAlliedForward = Subsystem->GetDiplomacyTags(SourceOwner, TargetOwner)
			.HasTagExact(SeinARTSTags::Diplomacy_Permission_Allied);
		const bool bAlliedReverse = Subsystem->GetDiplomacyTags(TargetOwner, SourceOwner)
			.HasTagExact(SeinARTSTags::Diplomacy_Permission_Allied);
		if (bAlliedForward && bAlliedReverse)
		{
			return;
		}
	}

	// Clamp damage to non-negative so "negative damage" (which would heal) must go
	// through SeinApplyHeal instead.
	if (Amount < FFixedPoint::Zero) Amount = FFixedPoint::Zero;

	const FFixedPoint PrevHealth = Combat->Health;
	Combat->Health = Combat->Health - Amount;
	if (Combat->Health < FFixedPoint::Zero) Combat->Health = FFixedPoint::Zero;
	const FFixedPoint ActualDelta = PrevHealth - Combat->Health;

	// Visual event + attribution.
	Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeDamageAppliedEvent(TargetHandle, SourceHandle, ActualDelta, DamageType));

	if (SourceOwner.IsValid())
	{
		USeinStatsBPFL::SeinBumpStat(WorldContextObject, SourceOwner, SeinARTSTags::Stat_TotalDamageDealt, ActualDelta);
	}
	if (TargetOwner.IsValid())
	{
		USeinStatsBPFL::SeinBumpStat(WorldContextObject, TargetOwner, SeinARTSTags::Stat_TotalDamageReceived, ActualDelta);
	}

	// Death flow — Health hit zero this call.
	if (Combat->Health <= FFixedPoint::Zero && PrevHealth > FFixedPoint::Zero)
	{
		// Fan out kill + death attribution before any ability fires so stat bumps
		// are guaranteed even if the death-handler destroys state we'd read.
		if (TargetOwner.IsValid())
		{
			USeinStatsBPFL::SeinBumpStat(WorldContextObject, TargetOwner, SeinARTSTags::Stat_UnitDeathCount, FFixedPoint::One);
			USeinStatsBPFL::SeinBumpStat(WorldContextObject, TargetOwner, SeinARTSTags::Stat_UnitsLost, FFixedPoint::One);
		}
		if (SourceOwner.IsValid() && SourceOwner != TargetOwner)
		{
			USeinStatsBPFL::SeinBumpStat(WorldContextObject, SourceOwner, SeinARTSTags::Stat_UnitKillCount, FFixedPoint::One);
			Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeKillEvent(SourceHandle, TargetHandle, SourceOwner));
		}

		Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeDeathEvent(TargetHandle, SourceHandle));

		// Optional death-handler ability: first ability on the entity carrying
		// the `SeinARTS.DeathHandler` tag in its QueryTags gets activated.
		// Ability then drives its own lifecycle via latent actions / OnEnd; the
		// entity is queued for destruction immediately regardless (the
		// handler's job is to express the death's gameplay consequences, not
		// to stall cleanup).
		if (FSeinAbilityData* Abilities = Subsystem->GetComponent<FSeinAbilityData>(TargetHandle))
		{
			for (USeinAbility* Ability : Abilities->AbilityInstances)
			{
				if (Ability && Ability->QueryTags.HasTagExact(SeinARTSTags::DeathHandler))
				{
					Ability->ActivateAbility(SourceHandle, Target->Transform.GetLocation());
					break;
				}
			}
		}

		Subsystem->DestroyEntity(TargetHandle);
	}
}

void USeinCombatBPFL::SeinApplyHeal(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FFixedPoint Amount, FGameplayTag HealType, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Target = Subsystem->GetEntity(TargetHandle);
	if (!Target || !Target->IsAlive()) return;

	FSeinCombatData* Combat = Subsystem->GetComponent<FSeinCombatData>(TargetHandle);
	if (!Combat) return;

	if (Amount < FFixedPoint::Zero) Amount = FFixedPoint::Zero;

	// Resolve MaxHealth with active modifiers applied so tech / effect buffs to
	// max-hp are respected; resolver is a no-op if no modifiers match.
	const FFixedPoint ResolvedMax = Subsystem->ResolveAttribute(TargetHandle, FSeinCombatData::StaticStruct(), TEXT("MaxHealth"));
	const FFixedPoint EffectiveMax = ResolvedMax > FFixedPoint::Zero ? ResolvedMax : Combat->MaxHealth;

	const FFixedPoint PrevHealth = Combat->Health;
	Combat->Health = Combat->Health + Amount;
	if (Combat->Health > EffectiveMax) Combat->Health = EffectiveMax;
	const FFixedPoint ActualDelta = Combat->Health - PrevHealth;

	Subsystem->EnqueueVisualEvent(FSeinVisualEvent::MakeHealAppliedEvent(TargetHandle, SourceHandle, ActualDelta, HealType));

	const FSeinPlayerID SourcePlayer = Subsystem->GetEntityOwner(SourceHandle);
	if (SourcePlayer.IsValid())
	{
		USeinStatsBPFL::SeinBumpStat(WorldContextObject, SourcePlayer, SeinARTSTags::Stat_TotalHealsDealt, ActualDelta);
	}
}

void USeinCombatBPFL::SeinApplySplashDamage(const UObject* WorldContextObject, FFixedVector Center, FFixedPoint Radius, FFixedPoint Amount, FFixedPoint Falloff, FGameplayTag DamageType, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || Radius <= FFixedPoint::Zero) return;

	const FFixedPoint RadiusSq = Radius * Radius;
	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();

	// Clamp falloff into [0, 1] so designers get sensible behavior from out-of-range inputs.
	if (Falloff < FFixedPoint::Zero) Falloff = FFixedPoint::Zero;
	if (Falloff > FFixedPoint::One) Falloff = FFixedPoint::One;

	TArray<TPair<FSeinEntityHandle, FFixedPoint>> Hits;
	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		const FFixedVector Delta = Entity.Transform.GetLocation() - Center;
		const FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);
		if (DistSq > RadiusSq) return;

		// Linear falloff: at center = Amount, at edge = Amount * (1 - Falloff).
		// Distance uses Size() (sqrt) for the falloff math — sim-cheap enough at
		// typical splash counts; callers worried about perf can implement their
		// own distance-squared curve and skip this helper.
		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint DistRatio = Radius > FFixedPoint::Zero ? (Dist / Radius) : FFixedPoint::Zero;
		const FFixedPoint Mul = FFixedPoint::One - (Falloff * DistRatio);
		const FFixedPoint HitAmount = Amount * Mul;
		if (HitAmount > FFixedPoint::Zero)
		{
			Hits.Add({ Handle, HitAmount });
		}
	});

	for (const TPair<FSeinEntityHandle, FFixedPoint>& Hit : Hits)
	{
		SeinApplyDamage(WorldContextObject, Hit.Key, Hit.Value, DamageType, SourceHandle);
	}
}

// =====================================================================
// Generic mutation escape hatch
// =====================================================================

bool USeinCombatBPFL::SeinMutateAttribute(const UObject* WorldContextObject, FSeinEntityHandle Target, UScriptStruct* ComponentStruct, FName FieldName, FFixedPoint Delta)
{
	if (!ComponentStruct) return false;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(ComponentStruct);
	if (!Storage) return false;

	void* CompData = Storage->GetComponentRaw(Target);
	if (!CompData) return false;

	const FFixedPoint Current = FSeinAttributeResolver::ReadFixedPointField(CompData, ComponentStruct, FieldName);
	FSeinAttributeResolver::WriteFixedPointField(CompData, ComponentStruct, FieldName, Current + Delta);
	return true;
}

// =====================================================================
// PRNG
// =====================================================================

bool USeinCombatBPFL::SeinRollAccuracy(const UObject* WorldContextObject, FFixedPoint BaseAccuracy)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	if (BaseAccuracy <= FFixedPoint::Zero) return false;
	if (BaseAccuracy >= FFixedPoint::One) return true;
	return Subsystem->SimRandom.Bool(BaseAccuracy);
}

// =====================================================================
// Spatial / distance helpers (unchanged)
// =====================================================================

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
