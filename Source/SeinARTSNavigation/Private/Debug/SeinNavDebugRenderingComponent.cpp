#include "Debug/SeinNavDebugRenderingComponent.h"

#include "Volumes/SeinNavVolume.h"
#include "SeinNavigationGrid.h"
#include "SeinNavigationSubsystem.h"
#include "Grid/SeinNavigationGridAsset.h"
#include "Grid/SeinNavigationLayer.h"
#include "Grid/SeinCellData.h"

#include "DebugRenderSceneProxy.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ShowFlags.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

#if UE_ENABLE_DEBUG_DRAWING

namespace
{
	/** Per-layer color tint. Layer 0 = vivid green/red (ground). Higher layers shift hue so
	 *  an overpass on layer N is easy to tell apart from the underpass on layer 0 at the
	 *  same XY. Order matches the ground-first layer indexing enforced in the baker. */
	static const FColor GWalkColors[]  = {
		FColor( 40, 230,  70),   // Layer 0 — vivid green  (ground)
		FColor( 70, 230, 200),   // Layer 1 — green-cyan
		FColor(120, 180, 230),   // Layer 2 — cyan-blue
		FColor(180, 140, 230),   // Layer 3 — purple
	};
	static const FColor GBlockColors[] = {
		FColor(230,  40,  30),   // Layer 0
		FColor(230, 120, 170),   // Layer 1
		FColor(230, 160,  40),   // Layer 2
		FColor(200,  40, 230),   // Layer 3
	};
	constexpr int32 GNumLayerColors = UE_ARRAY_COUNT(GWalkColors);

/**
 * FSeinNavDebugSceneProxy — wireframe boxes over each baked nav cell in the owning
 * volume's XY footprint. GetViewRelevance is the whole point: the proxy stays alive
 * regardless of show-flag state, and each FSceneView's view-family flags decide
 * whether it draws — so P / Show > Navigation / SeinARTS.Navigation.Debug all toggle
 * per-viewport independently, matching UE's native FNavMeshSceneProxy pattern.
 */
class FSeinNavDebugSceneProxy final : public FDebugRenderSceneProxy
{
public:
	FSeinNavDebugSceneProxy(const UPrimitiveComponent* InComp, const ASeinNavVolume* Volume,
	                        const TArray<FSeinNavigationLayer>* Layers,
	                        float CellSizeWorld, float OriginX, float OriginY)
		: FDebugRenderSceneProxy(InComp)
	{
		ViewFlagName = TEXT("Navigation");
		ViewFlagIndex = static_cast<uint32>(FEngineShowFlags::FindIndexByName(TEXT("Navigation")));

		if (!Volume || !Layers || Layers->Num() == 0 || CellSizeWorld <= 0.0f) { return; }

		const FBox VolumeBounds = Volume->GetVolumeWorldBounds();
		if (!VolumeBounds.IsValid) { return; }

		const float Half = CellSizeWorld * 0.5f;
		const float Inset = 2.0f;
		const float ExtentXY = Half - Inset;
		const float ExtentZ  = 1.0f;

		// Reserve — ideal upper bound is cells-in-volume-footprint-per-layer. Shrink
		// after to free unused capacity once sparse layers are done.
		int32 Reserve = 0;
		for (const FSeinNavigationLayer& L : *Layers) { Reserve += L.Width * L.Height; }
		Boxes.Reserve(Reserve);

		for (int32 LayerIdx = 0; LayerIdx < Layers->Num(); ++LayerIdx)
		{
			const FSeinNavigationLayer& Layer = (*Layers)[LayerIdx];
			if (Layer.Width == 0 || Layer.Height == 0) { continue; }

			const FColor WalkColor  = GWalkColors [FMath::Min(LayerIdx, GNumLayerColors - 1)];
			const FColor BlockColor = GBlockColors[FMath::Min(LayerIdx, GNumLayerColors - 1)];

			// Clip iteration to the volume's XY footprint (layers are per-map; one volume's
			// viz should only paint within its own brush).
			const int32 MinX = FMath::Clamp(FMath::FloorToInt((VolumeBounds.Min.X - OriginX) / CellSizeWorld), 0, Layer.Width - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt((VolumeBounds.Min.Y - OriginY) / CellSizeWorld), 0, Layer.Height - 1);
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt ((VolumeBounds.Max.X - OriginX) / CellSizeWorld), 0, Layer.Width - 1);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt ((VolumeBounds.Max.Y - OriginY) / CellSizeWorld), 0, Layer.Height - 1);

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const int32 CellIdx = Y * Layer.Width + X;
					const uint8 Flags = Layer.GetCellFlags(CellIdx);

					// Skip cells never written on this layer (default-zeroed). Without this,
					// layer 1+ would flood red across every XY the multi-layer bake didn't
					// cover on that layer.
					if (!(Flags & SeinCellFlag::BakeProcessed)) { continue; }

					const bool bWalkable = ((Flags & SeinCellFlag::Walkable) != 0)
						&& !(Flags & SeinCellFlag::DynamicBlocked);
					const float Z = Layer.GetCellWorldZ(CellIdx) + 2.0f;

					const FVector Center(
						OriginX + X * CellSizeWorld + Half,
						OriginY + Y * CellSizeWorld + Half,
						Z);
					const FVector Ext(ExtentXY, ExtentXY, ExtentZ);

					Boxes.Emplace(FBox(Center - Ext, Center + Ext), bWalkable ? WalkColor : BlockColor);
				}
			}
		}

		Boxes.Shrink();
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		const bool bVisible = View && View->Family && View->Family->EngineShowFlags.Navigation;
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = bVisible && IsShown(View);
		Result.bDynamicRelevance = bVisible;
		Result.bSeparateTranslucency = Result.bNormalTranslucency = bVisible && IsShown(View);
		return Result;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return static_cast<uint32>(sizeof(*this) + Boxes.GetAllocatedSize());
	}
};

} // namespace

#endif // UE_ENABLE_DEBUG_DRAWING

USeinNavDebugRenderingComponent::USeinNavDebugRenderingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	bIsEditorOnly = true;
	bSelectable = false;
}

bool USeinNavDebugRenderingComponent::IsNavigationShowFlagSet(const UWorld* World)
{
	if (!World || !GEngine) { return false; }

	FWorldContext* Ctx = GEngine->GetWorldContextFromWorld(World);

#if WITH_EDITOR
	if (GEditor && Ctx && Ctx->WorldType != EWorldType::Game)
	{
		if (Ctx->GameViewport && Ctx->GameViewport->EngineShowFlags.Navigation) { return true; }
		// GetAllViewportClients (not GetLevelViewportClients) matches UE's native
		// UNavMeshRenderingComponent::IsNavigationShowFlagSet. The comment there:
		// "we have to check all viewports because we can't distinguish between SIE and PIE
		// at this point." Picks up asset-editor preview viewports too, which is fine —
		// each FSceneView still gates rendering on its own EngineShowFlags.Navigation via
		// the scene proxy's GetViewRelevance.
		for (FEditorViewportClient* Vp : GEditor->GetAllViewportClients())
		{
			if (Vp && Vp->EngineShowFlags.Navigation) { return true; }
		}
		return false;
	}
#endif
	return Ctx && Ctx->GameViewport && Ctx->GameViewport->EngineShowFlags.Navigation;
}

void USeinNavDebugRenderingComponent::OnRegister()
{
	Super::OnRegister();

#if UE_ENABLE_DEBUG_DRAWING
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(
			TimerHandle,
			FTimerDelegate::CreateUObject(this, &USeinNavDebugRenderingComponent::TimerFunction),
			1.0f, /*bLoop=*/true);
	}
	else
#endif
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			TimerHandle,
			FTimerDelegate::CreateUObject(this, &USeinNavDebugRenderingComponent::TimerFunction),
			1.0f, /*bLoop=*/true);
	}
#endif
}

void USeinNavDebugRenderingComponent::OnUnregister()
{
#if UE_ENABLE_DEBUG_DRAWING
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(TimerHandle);
	}
	else
#endif
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}
#endif
	Super::OnUnregister();
}

void USeinNavDebugRenderingComponent::TimerFunction()
{
#if UE_ENABLE_DEBUG_DRAWING
	const UWorld* World = GetWorld();
	if (!World) { return; }

	const bool bShowNow = IsNavigationShowFlagSet(World);

	// Rebuild the proxy whenever:
	//   - ForceUpdate was set (bake finished, cell mutation happened)
	//   - the flag just transitioned ON (we skip proxy build when off; see CreateDebugSceneProxy)
	if (bForceUpdate || (bShowNow && !bCollectDebugData))
	{
		bForceUpdate = false;
		bCollectDebugData = bShowNow;
		MarkRenderStateDirty();
	}
	else if (!bShowNow && bCollectDebugData)
	{
		// Flag transitioned off — drop our data so the proxy is cheap.
		bCollectDebugData = false;
		MarkRenderStateDirty();
	}
#endif
}

#if UE_ENABLE_DEBUG_DRAWING
FDebugRenderSceneProxy* USeinNavDebugRenderingComponent::CreateDebugSceneProxy()
{
	const ASeinNavVolume* Volume = Cast<ASeinNavVolume>(GetOwner());
	if (!Volume) { return nullptr; }

	const UWorld* World = GetWorld();
	if (!World) { return nullptr; }

	// No point paying for the cell iterate if the flag is globally off. Timer's
	// ON-transition path calls MarkRenderStateDirty to recreate the proxy with data.
	const bool bShowNavigation = IsNavigationShowFlagSet(World);
	bCollectDebugData = bShowNavigation;
	if (!bShowNavigation) { return nullptr; }

	// Runtime subsystem is authoritative once the game world is running (picks up
	// dynamic footprint / navlink mutations); asset is the fallback + the editor-
	// preview source. Mirrors the resolve order the original tick-based viz used.
	const TArray<FSeinNavigationLayer>* Layers = nullptr;
	float CellSizeWorld = 0.0f;
	float OriginX = 0.0f, OriginY = 0.0f;

	auto TryAsset = [&](const USeinNavigationGridAsset* A) -> bool
	{
		if (!A || A->Layers.Num() == 0) { return false; }
		Layers = &A->Layers;
		CellSizeWorld = A->CellSize;
		OriginX = A->GridOrigin.X;
		OriginY = A->GridOrigin.Y;
		return true;
	};
	auto TryRuntime = [&]() -> bool
	{
		const USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>();
		const USeinNavigationGrid* Grid = Nav ? Nav->GetGrid() : nullptr;
		if (!Grid || Grid->Layers.Num() == 0) { return false; }
		Layers = &Grid->Layers;
		CellSizeWorld = Grid->CellSize.ToFloat();
		OriginX = Grid->GridOrigin.X.ToFloat();
		OriginY = Grid->GridOrigin.Y.ToFloat();
		return true;
	};

	if (World->IsGameWorld())
	{
		if (!TryRuntime()) { TryAsset(Volume->BakedGridAsset); }
	}
	else
	{
		if (!TryAsset(Volume->BakedGridAsset)) { TryRuntime(); }
	}

	if (!Layers || Layers->Num() == 0 || CellSizeWorld <= 0.0f) { return nullptr; }

	return new FSeinNavDebugSceneProxy(this, Volume, Layers, CellSizeWorld, OriginX, OriginY);
}
#endif

FBoxSphereBounds USeinNavDebugRenderingComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (const ASeinNavVolume* Volume = Cast<ASeinNavVolume>(GetOwner()))
	{
		const FBox VolumeBounds = Volume->GetVolumeWorldBounds();
		if (VolumeBounds.IsValid)
		{
			// Volume bounds are world-space; return them directly. Matches the pattern
			// UNavMeshRenderingComponent::CalcBounds uses (returns world-space navmesh bounds
			// ignoring LocalToWorld). The scene proxy's box data is also world-space with
			// identity FDebugBox::Transform, so frustum culling lines up.
			return FBoxSphereBounds(VolumeBounds);
		}
	}
	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0f);
}
