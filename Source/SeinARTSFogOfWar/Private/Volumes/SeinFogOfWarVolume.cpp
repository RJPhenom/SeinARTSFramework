/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolume.cpp
 */

#include "Volumes/SeinFogOfWarVolume.h"
#include "Debug/SeinFogOfWarDebugComponent.h"
#include "Settings/PluginSettings.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinFogOfWarVolume::ASeinFogOfWarVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	// Distinct color from the nav volume's orange so the two are visually
	// distinguishable when both sit on the same level. Teal / cyan reads as
	// "vision-related" against the terrain palette.
	BrushColor = FColor(0, 200, 200, 255);
	bColored = true;

#if UE_ENABLE_DEBUG_DRAWING
	// Scene-proxy debug viz. Visible in editor pre-PIE when the FogOfWar
	// show flag is on; null + stripped in shipping.
	DebugComponent = CreateDefaultSubobject<USeinFogOfWarDebugComponent>(TEXT("FogOfWarDebugRenderer"));
	if (DebugComponent && GetBrushComponent())
	{
		DebugComponent->SetupAttachment(GetBrushComponent());
	}
#endif
}

FBox ASeinFogOfWarVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}

float ASeinFogOfWarVolume::GetResolvedCellSize() const
{
	if (bOverrideCellSize) return CellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->VisionCellSize;
	}
	return 400.0f;
}
