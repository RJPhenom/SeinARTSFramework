/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinPlayerState.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		FSeinPlayerState implementation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Core/SeinPlayerState.h"
#include "Attributes/SeinModifier.h"

FSeinPlayerState::FSeinPlayerState(FSeinPlayerID InPlayerID, FSeinFactionID InFactionID, uint8 InTeamID)
	: PlayerID(InPlayerID)
	, FactionID(InFactionID)
	, TeamID(InTeamID)
	, bEliminated(false)
	, bReady(false)
{
}

bool FSeinPlayerState::CanAfford(const TMap<FName, FFixedPoint>& Cost) const
{
	for (const auto& Entry : Cost)
	{
		const FFixedPoint* Current = Resources.Find(Entry.Key);
		if (!Current || *Current < Entry.Value)
		{
			return false;
		}
	}
	return true;
}

bool FSeinPlayerState::DeductResources(const TMap<FName, FFixedPoint>& Cost)
{
	if (!CanAfford(Cost))
	{
		return false;
	}

	for (const auto& Entry : Cost)
	{
		FFixedPoint& Current = Resources.FindChecked(Entry.Key);
		Current = Current - Entry.Value;
	}
	return true;
}

void FSeinPlayerState::AddResources(const TMap<FName, FFixedPoint>& Amount)
{
	for (const auto& Entry : Amount)
	{
		FFixedPoint& Current = Resources.FindOrAdd(Entry.Key);
		Current = Current + Entry.Value;
	}
}

FFixedPoint FSeinPlayerState::GetResource(FName ResourceName) const
{
	const FFixedPoint* Found = Resources.Find(ResourceName);
	return Found ? *Found : FFixedPoint::Zero;
}

void FSeinPlayerState::SetResource(FName ResourceName, FFixedPoint Amount)
{
	Resources.FindOrAdd(ResourceName) = Amount;
}

// ==================== Tech Helpers ====================

bool FSeinPlayerState::HasAllTechTags(const FGameplayTagContainer& Required) const
{
	if (Required.IsEmpty())
	{
		return true;
	}
	return UnlockedTechTags.HasAll(Required);
}

void FSeinPlayerState::GrantTechTag(FGameplayTag Tag)
{
	if (Tag.IsValid())
	{
		UnlockedTechTags.AddTag(Tag);
	}
}

void FSeinPlayerState::RevokeTechTag(FGameplayTag Tag)
{
	if (Tag.IsValid())
	{
		UnlockedTechTags.RemoveTag(Tag);
	}
}

void FSeinPlayerState::AddArchetypeModifier(const FSeinModifier& Modifier)
{
	ArchetypeModifiers.Add(Modifier);
}

void FSeinPlayerState::RemoveArchetypeModifiersBySource(uint32 SourceEffectID)
{
	ArchetypeModifiers.RemoveAll([SourceEffectID](const FSeinModifier& Mod)
	{
		return Mod.SourceEffectID == SourceEffectID;
	});
}
