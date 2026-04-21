/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsData.cpp
 * @brief   FSeinActiveEffectsData implementation — effect add/remove,
 *          CDO-backed tag queries, stack counting, modifier collection
 *          for attribute resolve.
 */

#include "Components/SeinActiveEffectsData.h"
#include "Effects/SeinEffect.h"

namespace SeinActiveEffectsInternal
{
	FORCEINLINE const USeinEffect* CDO(const FSeinActiveEffect& Effect)
	{
		return Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
	}
}

uint32 FSeinActiveEffectsData::AddEffect(const FSeinActiveEffect& Effect)
{
	FSeinActiveEffect& Added = ActiveEffects.Add_GetRef(Effect);
	Added.EffectInstanceID = NextEffectInstanceID++;
	return Added.EffectInstanceID;
}

void FSeinActiveEffectsData::RemoveEffect(uint32 EffectInstanceID)
{
	for (int32 i = ActiveEffects.Num() - 1; i >= 0; --i)
	{
		if (ActiveEffects[i].EffectInstanceID == EffectInstanceID)
		{
			ActiveEffects.RemoveAtSwap(i, EAllowShrinking::No);
			return;
		}
	}
}

void FSeinActiveEffectsData::RemoveEffectsWithTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return;
	}

	for (int32 i = ActiveEffects.Num() - 1; i >= 0; --i)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(ActiveEffects[i]);
		if (Def && Def->EffectTag.MatchesTag(Tag))
		{
			ActiveEffects.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}
}

bool FSeinActiveEffectsData::HasEffectWithTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid())
	{
		return false;
	}

	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (Def && Def->EffectTag.MatchesTag(Tag))
		{
			return true;
		}
	}
	return false;
}

int32 FSeinActiveEffectsData::GetStackCountForTag(const FGameplayTag& Tag) const
{
	int32 Total = 0;
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (Def && Def->EffectTag.MatchesTag(Tag))
		{
			Total += Effect.CurrentStacks;
		}
	}
	return Total;
}

int32 FSeinActiveEffectsData::GetStackCountForClass(TSubclassOf<USeinEffect> EffectClass) const
{
	int32 Total = 0;
	if (!EffectClass) return 0;
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		if (Effect.EffectClass == EffectClass)
		{
			Total += Effect.CurrentStacks;
		}
	}
	return Total;
}

void FSeinActiveEffectsData::CollectModifiers(TArray<FSeinModifier>& OutModifiers) const
{
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (!Def) continue;

		for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
		{
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				FSeinModifier& Added = OutModifiers.Add_GetRef(Mod);
				Added.SourceEntity = Effect.Source;
				Added.SourceEffectID = Effect.EffectInstanceID;
			}
		}
	}
}

void FSeinActiveEffectsData::Clear()
{
	ActiveEffects.Empty();
	NextEffectInstanceID = 1;
}
