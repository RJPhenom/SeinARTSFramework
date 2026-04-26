/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarBPFL.cpp
 */

#include "Lib/SeinFogOfWarBPFL.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinVisionLayerDefinition.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/SeinVisionData.h"
#include "Types/Entity.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	/** Resolve a layer name to its EVNNNNNN bit index [0..7], or -1 if the
	 *  name isn't valid. "Normal" = V (1), "Explored" = E (0), anything
	 *  else matches enabled plugin-settings slots by LayerName. */
	static int32 ResolveLayerBit(FName LayerName)
	{
		if (LayerName == TEXT("Normal"))   return 1;
		if (LayerName == TEXT("Explored")) return 0;

		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings) return INDEX_NONE;
		const int32 MaxSlots = FMath::Min(Settings->VisionLayers.Num(), 6);
		for (int32 i = 0; i < MaxSlots; ++i)
		{
			const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[i];
			if (Def.bEnabled && Def.LayerName == LayerName)
			{
				return 2 + i;
			}
		}
		return INDEX_NONE;
	}

}

bool USeinFogOfWarBPFL::SeinIsCellVisible(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos, FName LayerName)
{
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return false;
	if (!Fog->HasRuntimeData()) return true; // no data = permit (matches LOS resolver)

	const int32 Bit = ResolveLayerBit(LayerName);
	if (Bit < 0) return false;

	const uint8 Mask = static_cast<uint8>(1u << Bit);
	const FFixedVector FixedPos = FFixedVector::FromVector(WorldPos);
	return Fog->IsCellVisible(Observer, FixedPos, Mask);
}

bool USeinFogOfWarBPFL::SeinIsCellExplored(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos)
{
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return false;
	return Fog->IsCellExplored(Observer, FFixedVector::FromVector(WorldPos));
}

uint8 USeinFogOfWarBPFL::SeinGetCellBitfield(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos)
{
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return 0;
	return Fog->GetCellBitfield(Observer, FFixedVector::FromVector(WorldPos));
}

bool USeinFogOfWarBPFL::SeinIsEntityVisible(const UObject* WorldContextObject,
	FSeinPlayerID Observer, FSeinEntityHandle Target)
{
	if (!Target.IsValid()) return false;
	if (!WorldContextObject) return false;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return false;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return false;
	if (!Sim->GetEntity(Target)) return false;

	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return false;
	if (!Fog->HasRuntimeData()) return true;

	// Emission-aware visibility: observer needs ANY stamped bit that
	// matches the target's EmissionLayerMask. Handles camo cleanly — a
	// unit that emits only on Stealth is invisible to Normal-only
	// observers even if the cell has V bit set, and visible to observers
	// perceiving Stealth even if V isn't stamped there.
	uint8 EmissionMask = SEIN_FOW_BIT_NORMAL;
	if (const ISeinComponentStorage* VisionStorage = Sim->GetComponentStorageRaw(FSeinVisionData::StaticStruct()))
	{
		if (const void* Raw = VisionStorage->GetComponentRaw(Target))
		{
			EmissionMask = static_cast<const FSeinVisionData*>(Raw)->EmissionLayerMask;
		}
	}
	if (EmissionMask == 0) return false; // entity authored as never-visible

	// Volumetric query — ORs cell bits across the target's extents. Falls
	// back to single-point at center when the target has no extents
	// component. Matters for big-footprint targets (tanks, buildings)
	// whose center may sit one cell away from the nearest visible cell
	// even when the entity is plainly in view.
	const uint8 ObserverBits = Fog->GetEntityVisibleBits(Observer, *Sim, Target);
	return (ObserverBits & EmissionMask) != 0;
}

FSeinPlayerID USeinFogOfWarBPFL::SeinGetLocalObserver(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return FSeinPlayerID();
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(World);
}

// ============================================================================
// Runtime mutation — emission mask
// ============================================================================

namespace
{
	/** Shared resolver: validate context + entity + get mutable FSeinVisionData
	 *  ptr. Returns nullptr on any failure (entity invalid, no sim, no vision
	 *  component). Returning via out-param keeps the happy-path branch-free. */
	static FSeinVisionData* FindMutableVisionData(const UObject* WorldContextObject,
		FSeinEntityHandle Entity)
	{
		if (!WorldContextObject || !Entity.IsValid()) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World) return nullptr;
		USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
		if (!Sim) return nullptr;
		ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinVisionData::StaticStruct());
		if (!Storage) return nullptr;
		return static_cast<FSeinVisionData*>(Storage->GetComponentRaw(Entity));
	}
}

int32 USeinFogOfWarBPFL::SeinGetEntityEmissionMask(const UObject* WorldContextObject,
	FSeinEntityHandle Entity)
{
	const FSeinVisionData* VData = FindMutableVisionData(WorldContextObject, Entity);
	return VData ? static_cast<int32>(VData->EmissionLayerMask) : 0;
}

void USeinFogOfWarBPFL::SeinSetEntityEmissionMask(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 NewMask)
{
	if (FSeinVisionData* VData = FindMutableVisionData(WorldContextObject, Entity))
	{
		VData->EmissionLayerMask = static_cast<uint8>(NewMask & 0xFF);
	}
}

void USeinFogOfWarBPFL::SeinAddEntityEmissionLayers(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 LayersToAdd)
{
	if (FSeinVisionData* VData = FindMutableVisionData(WorldContextObject, Entity))
	{
		VData->EmissionLayerMask |= static_cast<uint8>(LayersToAdd & 0xFF);
	}
}

void USeinFogOfWarBPFL::SeinRemoveEntityEmissionLayers(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 LayersToRemove)
{
	if (FSeinVisionData* VData = FindMutableVisionData(WorldContextObject, Entity))
	{
		VData->EmissionLayerMask &= ~static_cast<uint8>(LayersToRemove & 0xFF);
	}
}
