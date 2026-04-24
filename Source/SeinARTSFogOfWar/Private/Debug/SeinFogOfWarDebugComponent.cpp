/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDebugComponent.cpp
 *
 * Scene-proxy pattern mirrors USeinNavDebugComponent / UE's FNavMeshSceneProxy:
 *   - View relevance sets bSeparateTranslucency + bNormalTranslucency (the
 *     DebugMeshMaterial is translucent; bOpaque alone produces nothing).
 *   - Custom `ShowFlags.FogOfWar` lookup via `FindCustomShowFlagByName` +
 *     `GetSingleFlag(Index + SF_FirstCustom)` — the same dance `TCustomShowFlag::IsEnabled`
 *     does internally, done here without linking against the module's TU.
 *   - Cell layout: if the active impl has runtime data (bake / PIE stamp grid),
 *     route through `USeinFogOfWar::CollectDebugCellQuads`. Otherwise, rasterize
 *     the owner volume's XY bounds into cells at the volume's resolved cell
 *     size and downward-trace each cell for a terrain-Z snap — gives designers
 *     a "this volume covers these cells" red layer in editor preview without
 *     needing a bake. The per-cell trace is capped at 20k cells to keep proxy
 *     creation under ~100ms on typical hardware; beyond that we fall back to a
 *     flat plane at volume mid-Z.
 *
 * Shipping strip: scene-proxy class, method bodies, and render-specific
 * includes are gated on UE_ENABLE_DEBUG_DRAWING. The component class stays
 * in shipping (ABI + reflection) but every virtual compiles to a no-op stub.
 */

#include "Debug/SeinFogOfWarDebugComponent.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "Volumes/SeinFogOfWarVolume.h"

#include "Engine/World.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "MeshElementCollector.h"
#include "ShowFlags.h"
#include "Components/BrushComponent.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFogOfWarDebug, Log, All);

namespace
{
	/** Query the custom ShowFlags.FogOfWar state on an FEngineShowFlags.
	 *  `FindIndexByName` handles both engine and custom flags — it falls
	 *  through to the custom-flag lookup internally and returns a raw bit
	 *  index that includes SF_FirstCustom (so we can feed it directly to
	 *  GetSingleFlag). `FindCustomShowFlagByName` would be more direct but
	 *  is private to FEngineShowFlags in 5.7. */
	static bool IsFogOfWarShowFlagOn(const FEngineShowFlags& ShowFlags)
	{
		// The flag is registered by FSeinARTSFogOfWarModule at module load.
		// First-call init here happens well after module init for any code
		// path that reaches a scene proxy (viewport render), so the lookup
		// reliably resolves.
		static const int32 FlagBitIndex = FEngineShowFlags::FindIndexByName(TEXT("FogOfWar"));
		if (FlagBitIndex == INDEX_NONE) return false;
		return ShowFlags.GetSingleFlag(FlagBitIndex);
	}

	/** Rasterize an AABB into cell centers on the XY plane, with per-cell
	 *  downward trace for terrain-Z snap (capped at MaxTracedCells). Beyond
	 *  the cap, use a flat plane at the volume's mid-Z. Cells sit +20cm
	 *  above the hit to overlay cleanly on top of nav's debug layer. */
	static constexpr int32 MaxTracedCells = 20000;

	static void RasterizeVolumeCells(
		UWorld* World,
		const ASeinFogOfWarVolume* Volume,
		TArray<FVector>& OutCenters,
		float& OutHalfExtent)
	{
		if (!World || !Volume) return;

		const FBox Bounds = Volume->GetVolumeWorldBounds();
		if (!Bounds.IsValid || Bounds.GetSize().IsNearlyZero()) return;

		const float CellSize = Volume->GetResolvedCellSize();
		if (CellSize <= 0.0f) return;
		OutHalfExtent = CellSize * 0.5f;

		const int32 Width  = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().X / CellSize));
		const int32 Height = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().Y / CellSize));
		const int32 NumCells = Width * Height;
		const bool bTracePerCell = NumCells <= MaxTracedCells;
		const float FallbackZ = (Bounds.Min.Z + Bounds.Max.Z) * 0.5f;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinFowDebugTrace), /*bTraceComplex*/ true);
		Params.AddIgnoredActor(Volume);

		OutCenters.Reserve(NumCells);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float CX = Bounds.Min.X + (X + 0.5f) * CellSize;
				const float CY = Bounds.Min.Y + (Y + 0.5f) * CellSize;

				if (bTracePerCell)
				{
					FHitResult Hit;
					const FVector Start(CX, CY, Bounds.Max.Z + 100.0f);
					const FVector End  (CX, CY, Bounds.Min.Z - 100.0f);
					if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
					{
						OutCenters.Add(FVector(CX, CY, Hit.Location.Z + SEIN_FOW_DEBUG_Z_OFFSET));
						continue;
					}
				}
				OutCenters.Add(FVector(CX, CY, FallbackZ + SEIN_FOW_DEBUG_Z_OFFSET));
			}
		}
	}
}

// ============================================================================
// Scene proxy — two-batch pattern (visible cyan + unseen red), same shape as
// USeinNavDebugProxy's walkable/blocked split.
// ============================================================================

class FSeinFogOfWarDebugProxy final : public FPrimitiveSceneProxy
{
public:
	FSeinFogOfWarDebugProxy(
		UPrimitiveComponent* InComponent,
		TArray<FVector>&& InVisibleCenters,
		TArray<FVector>&& InUnseenCenters,
		float InHalfExtent)
		: FPrimitiveSceneProxy(InComponent)
		, VisibleCenters(MoveTemp(InVisibleCenters))
		, UnseenCenters(MoveTemp(InUnseenCenters))
		, HalfExtent(InHalfExtent)
	{
		bWillEverBeLit = false;
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + VisibleCenters.GetAllocatedSize() + UnseenCenters.GetAllocatedSize();
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		const bool bVisible = IsFogOfWarShowFlagOn(View->Family->EngineShowFlags);
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = bVisible && IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bSeparateTranslucency = Result.bNormalTranslucency = bVisible && IsShown(View);
		Result.bShadowRelevance = false;
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		return Result;
	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		UMaterialInterface* BaseMat = GEngine ? GEngine->DebugMeshMaterial : nullptr;
		if (!BaseMat) return;
		const FMaterialRenderProxy* BaseProxy = BaseMat->GetRenderProxy();
		if (!BaseProxy) return;
		if (VisibleCenters.Num() == 0 && UnseenCenters.Num() == 0) return;

		for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ++ViewIdx)
		{
			if (!(VisibilityMap & (1 << ViewIdx))) continue;
			const FSceneView* View = Views[ViewIdx];
			if (!IsFogOfWarShowFlagOn(View->Family->EngineShowFlags)) continue;

			const FColoredMaterialRenderProxy* CyanMat =
				&Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
					BaseProxy, FLinearColor(0.0f, 0.8f, 1.0f, 0.4f));
			const FColoredMaterialRenderProxy* RedMat =
				&Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
					BaseProxy, FLinearColor(0.85f, 0.0f, 0.0f, 0.35f));

			if (VisibleCenters.Num() > 0)
			{
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, VisibleCenters);
				Builder.GetMesh(FMatrix::Identity, CyanMat, SDPG_World,
					/*bDisableBackfaceCulling*/ true, /*bReceivesDecals*/ false,
					ViewIdx, Collector);
			}
			if (UnseenCenters.Num() > 0)
			{
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, UnseenCenters);
				Builder.GetMesh(FMatrix::Identity, RedMat, SDPG_World,
					true, false, ViewIdx, Collector);
			}
		}
	}

private:

	void EmitQuads(FDynamicMeshBuilder& Builder, const TArray<FVector>& Centers) const
	{
		Builder.ReserveVertices(Centers.Num() * 4);
		Builder.ReserveTriangles(Centers.Num() * 2);

		const FVector3f TangentX(1.0f, 0.0f, 0.0f);
		const FVector3f TangentY(0.0f, 1.0f, 0.0f);
		const FVector3f TangentZ(0.0f, 0.0f, 1.0f);

		for (const FVector& C : Centers)
		{
			FDynamicMeshVertex V0, V1, V2, V3;
			V0.Position = FVector3f(C + FVector(-HalfExtent, -HalfExtent, 0));
			V1.Position = FVector3f(C + FVector( HalfExtent, -HalfExtent, 0));
			V2.Position = FVector3f(C + FVector( HalfExtent,  HalfExtent, 0));
			V3.Position = FVector3f(C + FVector(-HalfExtent,  HalfExtent, 0));
			V0.TextureCoordinate[0] = FVector2f(0, 0);
			V1.TextureCoordinate[0] = FVector2f(1, 0);
			V2.TextureCoordinate[0] = FVector2f(1, 1);
			V3.TextureCoordinate[0] = FVector2f(0, 1);
			V0.SetTangents(TangentX, TangentY, TangentZ);
			V1.SetTangents(TangentX, TangentY, TangentZ);
			V2.SetTangents(TangentX, TangentY, TangentZ);
			V3.SetTangents(TangentX, TangentY, TangentZ);

			const int32 Base = Builder.AddVertex(V0);
			Builder.AddVertex(V1);
			Builder.AddVertex(V2);
			Builder.AddVertex(V3);
			Builder.AddTriangle(Base + 0, Base + 1, Base + 2);
			Builder.AddTriangle(Base + 0, Base + 2, Base + 3);
		}
	}

	TArray<FVector> VisibleCenters;
	TArray<FVector> UnseenCenters;
	float HalfExtent;
};

#endif // UE_ENABLE_DEBUG_DRAWING

// ============================================================================
// Component
// ============================================================================

USeinFogOfWarDebugComponent::USeinFogOfWarDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bSelectable = false;
	bUseAsOccluder = false;
	bCastDynamicShadow = false;
	bCastStaticShadow = false;
	bHiddenInGame = false;
	SetMobility(EComponentMobility::Static);
}

FPrimitiveSceneProxy* USeinFogOfWarDebugComponent::CreateSceneProxy()
{
#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	ASeinFogOfWarVolume* Volume = Cast<ASeinFogOfWarVolume>(GetOwner());
	if (!Volume) return nullptr;

	TArray<FVector> AllCenters;
	TArray<FColor> AllColors;
	float HalfExtent = 0.0f;

	// Impl-path: when the fog subsystem has runtime data (bake loaded / PIE
	// stamp grid active), route through the impl's cell collector. Falls back
	// to volume-bounds rasterization in non-PIE / pre-bake / fog-less games —
	// everything red (no vision).
	if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
	{
		if (USeinFogOfWar* Fog = Sub->GetFogOfWar())
		{
			if (Fog->HasRuntimeData())
			{
				// TODO(ownership): pass the local PC's FSeinPlayerID so the
				// impl scopes to that observer. MVP impl is global so the
				// Observer arg is ignored.
				Fog->CollectDebugCellQuads(FSeinPlayerID(), AllCenters, AllColors, HalfExtent);
			}
		}
	}

	if (AllCenters.Num() == 0)
	{
		// Fallback: rasterize volume bounds — all red (no vision, no bake).
		RasterizeVolumeCells(World, Volume, AllCenters, HalfExtent);
		// No colors array → treat everything as unseen (red).
	}

	if (AllCenters.Num() == 0) return nullptr;

	// Bucket cells by color: cyan (visible) vs everything else (unseen).
	// Impl uses FColor(0, 200, 255) for cyan and (200, 0, 0) for red — test
	// on blue > red is robust for the MVP 2-color scheme. When explored-only
	// (dim red) comes in, bucket logic expands to 3 buckets.
	TArray<FVector> VisibleCenters;
	TArray<FVector> UnseenCenters;
	if (AllColors.Num() == AllCenters.Num())
	{
		VisibleCenters.Reserve(AllCenters.Num());
		UnseenCenters.Reserve(AllCenters.Num());
		for (int32 i = 0; i < AllCenters.Num(); ++i)
		{
			const FColor& C = AllColors[i];
			if (C.B > C.R) VisibleCenters.Add(AllCenters[i]);
			else           UnseenCenters.Add(AllCenters[i]);
		}
	}
	else
	{
		UnseenCenters = MoveTemp(AllCenters);
	}

	UE_LOG(LogSeinFogOfWarDebug, Verbose,
		TEXT("CreateSceneProxy: %d visible + %d unseen cells, halfExtent=%.1f"),
		VisibleCenters.Num(), UnseenCenters.Num(), HalfExtent);

	return new FSeinFogOfWarDebugProxy(this,
		MoveTemp(VisibleCenters), MoveTemp(UnseenCenters), HalfExtent);
#else
	return nullptr;
#endif
}

FBoxSphereBounds USeinFogOfWarDebugComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FBox(FVector(-1048576.0), FVector(1048576.0)));
}

void USeinFogOfWarDebugComponent::OnRegister()
{
	Super::OnRegister();

#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return;
	if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
	{
		if (USeinFogOfWar* Fog = Sub->GetFogOfWar())
		{
			SubscribedFog = Fog;
			FogMutatedHandle = Fog->OnFogOfWarMutated.AddUObject(this, &USeinFogOfWarDebugComponent::HandleFogMutated);
		}
	}
	MarkRenderStateDirty();
#endif
}

void USeinFogOfWarDebugComponent::OnUnregister()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (USeinFogOfWar* Fog = SubscribedFog.Get())
	{
		Fog->OnFogOfWarMutated.Remove(FogMutatedHandle);
	}
	SubscribedFog.Reset();
	FogMutatedHandle.Reset();
#endif
	Super::OnUnregister();
}

void USeinFogOfWarDebugComponent::HandleFogMutated()
{
#if UE_ENABLE_DEBUG_DRAWING
	MarkRenderStateDirty();
#endif
}
