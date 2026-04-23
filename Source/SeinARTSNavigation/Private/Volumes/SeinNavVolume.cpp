/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolume.cpp
 */

#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Debug/SeinNavDebugComponent.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinNavVolume::ASeinNavVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Match ANavMeshBoundsVolume: NoCollision profile keeps the FBrushSceneProxy
	// orange-wireframe rendering in editor while guaranteeing no physics/query.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(255, 165, 0, 255); // orange
	bColored = true;

#if UE_ENABLE_DEBUG_DRAWING
	// Scene-proxy debug viz. Visible in editor pre-PIE when the Navigation
	// show flag is on; null + stripped in shipping.
	DebugComponent = CreateDefaultSubobject<USeinNavDebugComponent>(TEXT("NavDebugRenderer"));
	if (DebugComponent && GetBrushComponent())
	{
		DebugComponent->SetupAttachment(GetBrushComponent());
	}
#endif
}

FBox ASeinNavVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}

float ASeinNavVolume::GetResolvedCellSize() const
{
	if (bOverrideCellSize) return CellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->DefaultCellSize;
	}
	return 100.0f;
}
