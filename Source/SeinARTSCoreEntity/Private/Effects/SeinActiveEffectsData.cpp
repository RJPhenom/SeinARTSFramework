/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsData.cpp
 * @brief   FSeinActiveEffectsData sim-payload implementation — effect add/remove,
 *          tag queries, stack counting, modifier collection for attribute resolve.
 */

#include "Components/SeinActiveEffectsData.h"

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
		if (ActiveEffects[i].Definition.EffectTag.MatchesTag(Tag))
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
		if (Effect.Definition.EffectTag.MatchesTag(Tag))
		{
			return true;
		}
	}
	return false;
}

int32 FSeinActiveEffectsData::GetStackCount(FName EffectName) const
{
	int32 TotalStacks = 0;
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		if (Effect.Definition.EffectName == EffectName)
		{
			TotalStacks += Effect.CurrentStacks;
		}
	}
	return TotalStacks;
}

void FSeinActiveEffectsData::CollectModifiers(TArray<FSeinModifier>& OutModifiers) const
{
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
		{
			for (const FSeinModifier& Mod : Effect.Definition.Modifiers)
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
