#include "Volumes/SeinTerrainVolume.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinTerrainVolume::ASeinTerrainVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Match ANavMeshBoundsVolume exactly — see SeinNavVolume.cpp for rationale.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(255, 180, 100, 255); // warm tan — distinguishes from NavVolume orange
	bColored = true;
}

FBox ASeinTerrainVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}
