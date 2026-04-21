#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Bake/SeinNavBaker.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinNavDebugRenderingComponent.h"
#endif

ASeinNavVolume::ASeinNavVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Match ANavMeshBoundsVolume exactly. The brush's "NoCollision" profile
	// keeps the component's SceneProxy alive (so FBrushSceneProxy renders the
	// orange wireframe in editor) while guaranteeing no physics/query impact.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(255, 165, 0, 255); // orange
	bColored = true;

#if UE_ENABLE_DEBUG_DRAWING
	// UPrimitiveComponent — attach to the brush (the volume's root) so component lifetime
	// and registration follow the actor. The scene proxy renders world-space cell boxes
	// and ignores LocalToWorld, so the attachment is purely for bookkeeping.
	DebugRenderComponent = CreateDefaultSubobject<USeinNavDebugRenderingComponent>(TEXT("DebugRenderer"));
	if (DebugRenderComponent && GetBrushComponent())
	{
		DebugRenderComponent->SetupAttachment(GetBrushComponent());
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
	if (bOverrideCellSize) { return CellSize; }
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->DefaultCellSize;
	}
	return 100.0f;
}

ESeinElevationMode ASeinNavVolume::GetResolvedElevationMode() const
{
	if (bOverrideElevationMode) { return ElevationMode; }
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->DefaultElevationMode;
	}
	return ESeinElevationMode::None;
}

float ASeinNavVolume::GetResolvedLayerSeparation() const
{
	if (bOverrideLayerSeparation) { return LayerSeparation; }
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->DefaultLayerSeparation;
	}
	return 300.0f;
}

#if WITH_EDITOR

void ASeinNavVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Auto-bake on drag-end so resize immediately reflects in the nav data.
	if (bFinished)
	{
		if (UWorld* World = GetWorld())
		{
			USeinNavBaker::BeginBake(World);
		}
	}
}

void ASeinNavVolume::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	const FName PropertyName = Event.Property ? Event.Property->GetFName() : NAME_None;
	const bool bBoundsProperty =
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, bOverrideCellSize) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, CellSize) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, bOverrideElevationMode) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, ElevationMode) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, bOverrideLayerSeparation) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(ASeinNavVolume, LayerSeparation);

	if (bBoundsProperty)
	{
		if (UWorld* World = GetWorld())
		{
			USeinNavBaker::BeginBake(World);
		}
	}
}

#endif
