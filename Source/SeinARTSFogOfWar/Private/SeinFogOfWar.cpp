/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.cpp
 */

#include "SeinFogOfWar.h"
#include "SeinFogOfWarAsset.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

TSubclassOf<USeinFogOfWarAsset> USeinFogOfWar::GetAssetClass() const
{
	return USeinFogOfWarAsset::StaticClass();
}

uint8 USeinFogOfWar::GetEntityVisibleBits(FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const
{
	// Single-point fallback. Subclasses override to do the volumetric
	// (extents-aware) sweep.
	const FSeinEntity* Entity = Sim.GetEntity(Target);
	if (!Entity) return 0;
	return GetCellBitfield(Observer, Entity->Transform.GetLocation());
}
