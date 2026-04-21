#include "Bake/SeinNavBaker.h"

#include "Volumes/SeinNavVolume.h"
#include "Volumes/SeinNavLinkProxy.h"
#include "Volumes/SeinTerrainVolume.h"
#include "SeinNavigationGrid.h"
#include "Components/SeinNavModifierComponent.h"
#include "Grid/SeinNavigationGridAsset.h"
#include "Grid/SeinAbstractGraph.h"
#include "Settings/PluginSettings.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinNavDebugRenderingComponent.h"
#endif

#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "Misc/FeedbackContext.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSeinNavBake, Log, All);

TWeakObjectPtr<USeinNavBaker> USeinNavBaker::ActiveBaker;

bool USeinNavBaker::IsBaking()
{
	return ActiveBaker.IsValid();
}

USeinNavBaker* USeinNavBaker::BeginBake(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogSeinNavBake, Warning, TEXT("BeginBake called with null World."));
		return nullptr;
	}
	if (IsBaking())
	{
		UE_LOG(LogSeinNavBake, Warning, TEXT("Nav bake already in progress — ignoring new request."));
		return nullptr;
	}

	// Collect all nav volumes.
	TArray<ASeinNavVolume*> Volumes;
	for (TActorIterator<ASeinNavVolume> It(World); It; ++It)
	{
		Volumes.Add(*It);
	}

	if (Volumes.Num() == 0)
	{
		UE_LOG(LogSeinNavBake, Warning, TEXT("No ASeinNavVolume actors in world; nothing to bake."));
		return nullptr;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();

	// Resolve params from the first volume; all volumes must agree (validated below).
	ASeinNavVolume* First = Volumes[0];
	FSeinNavBakeParams Params;
	Params.CellSize = First->GetResolvedCellSize();
	Params.ElevationMode = First->GetResolvedElevationMode();
	Params.LayerSeparation = First->GetResolvedLayerSeparation();
	Params.TileSize = Settings ? Settings->NavTileSize : 32;
	Params.AgentRadiusClasses = Settings ? Settings->AgentRadiusClasses : TArray<float>();
	if (Settings)
	{
		const float SlopeRad = FMath::DegreesToRadians(Settings->MaxWalkableSlopeDegrees);
		Params.MaxWalkableSlopeCos = FMath::Cos(SlopeRad);
	}

	// Union bounds + validate volume agreement.
	FBox Union(ForceInit);
	for (ASeinNavVolume* Vol : Volumes)
	{
		if (!FMath::IsNearlyEqual(Vol->GetResolvedCellSize(), Params.CellSize))
		{
			UE_LOG(LogSeinNavBake, Error, TEXT("NavVolume '%s' cell-size mismatch (%.1f vs baseline %.1f)"),
				*Vol->GetName(), Vol->GetResolvedCellSize(), Params.CellSize);
			return nullptr;
		}
		if (Vol->GetResolvedElevationMode() != Params.ElevationMode)
		{
			UE_LOG(LogSeinNavBake, Error, TEXT("NavVolume '%s' elevation-mode mismatch"), *Vol->GetName());
			return nullptr;
		}
		Union += Vol->GetVolumeWorldBounds();
	}

	Params.UnionWorldBounds = Union;
	Params.GridWidth = FMath::Max(1, FMath::CeilToInt(Union.GetSize().X / Params.CellSize));
	Params.GridHeight = FMath::Max(1, FMath::CeilToInt(Union.GetSize().Y / Params.CellSize));
	Params.GridOriginWorld = FVector(Union.Min.X, Union.Min.Y, Union.Min.Z);
	Params.TopOfBake = FVector(0.0f, 0.0f, Union.Max.Z + 10.0f);
	Params.TotalHeight = Union.Max.Z - Union.Min.Z;

	// Allocate baker.
	USeinNavBaker* Baker = NewObject<USeinNavBaker>(GetTransientPackage());
	Baker->AddToRoot();
	Baker->WorldPtr = World;
	for (ASeinNavVolume* Vol : Volumes) { Baker->Volumes.Add(Vol); }
	Baker->Params = Params;
	Baker->TilesPerStep = Settings ? Settings->BakeTilesPerProgressStep : 16;

	// Allocate asset.
	USeinNavigationGridAsset* Asset = NewObject<USeinNavigationGridAsset>(GetTransientPackage());
	Asset->GridWidth = Params.GridWidth;
	Asset->GridHeight = Params.GridHeight;
	Asset->CellSize = Params.CellSize;
	Asset->GridOrigin = Params.GridOriginWorld;
	Asset->TileSize = Params.TileSize;
	Asset->ElevationMode = Params.ElevationMode;
	Asset->CellTagPalette.Add(FGameplayTag()); // palette slot 0 = null

	// Layer 0 (ground). Multi-layer stacking is filled in as traces find Z bands.
	FSeinNavigationLayer GroundLayer;
	GroundLayer.LayerBaselineZ = Union.Min.Z;
	GroundLayer.Allocate(Params.GridWidth, Params.GridHeight, Params.ElevationMode, Params.TileSize);
	Asset->Layers.Add(MoveTemp(GroundLayer));
	Baker->BakedAsset = Asset;

	// Tile index for progress.
	const FSeinNavigationLayer& L0 = Asset->Layers[0];
	Baker->TotalTiles = L0.GetTileWidth() * L0.GetTileHeight();
	Baker->NextTileIndex = 0;

	// Hit-Z accumulators — parallel to Asset->Layers, grown dynamically as multi-layer traces
	// discover new layers. After the trace phase finishes, the per-layer average is written to
	// LayerBaselineZ so Flat-mode cells render at actual floor Z (not volume's Min.Z).
	Baker->LayerHitZSum.Init(0.0, Asset->Layers.Num());
	Baker->LayerHitZCount.Init(0, Asset->Layers.Num());

	// Toast + ticker.
	Baker->ShowToast(NSLOCTEXT("SeinARTSNavigation", "BakeStart", "Rebuilding SeinARTS nav…"));
	Baker->TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(Baker, &USeinNavBaker::TickBake),
		0.0f);

	ActiveBaker = Baker;
	UE_LOG(LogSeinNavBake, Log,
		TEXT("Bake started: %dx%d cells, %d tiles, cell=%.1f cm, elevation=%s, layerSep=%.1f cm, multi-layer=%s"),
		Params.GridWidth, Params.GridHeight, Baker->TotalTiles, Params.CellSize,
		Params.ElevationMode == ESeinElevationMode::None        ? TEXT("None") :
		Params.ElevationMode == ESeinElevationMode::HeightOnly  ? TEXT("HeightOnly") :
		                                                          TEXT("HeightSlope"),
		Params.LayerSeparation, Params.IsMultiLayer() ? TEXT("yes") : TEXT("no"));
	UE_LOG(LogSeinNavBake, Log,
		TEXT("Volume Z range: [%.1f, %.1f] (tracing %.1f cm vertically)"),
		Params.UnionWorldBounds.Min.Z, Params.UnionWorldBounds.Max.Z, Params.TotalHeight);
	return Baker;
}

void USeinNavBaker::RequestCancel()
{
	bCancelRequested = true;
}

bool USeinNavBaker::TickBake(float /*DeltaTime*/)
{
	if (bCancelRequested)
	{
		DismissToast(false, NSLOCTEXT("SeinARTSNavigation", "BakeCancelled", "SeinARTS nav bake cancelled."));
		ActiveBaker.Reset();
		RemoveFromRoot();
		return false;
	}

	if (!WorldPtr.IsValid() || !BakedAsset)
	{
		DismissToast(false, NSLOCTEXT("SeinARTSNavigation", "BakeFailed", "SeinARTS nav bake failed (world/asset lost)."));
		ActiveBaker.Reset();
		RemoveFromRoot();
		return false;
	}

	// Phase 1: trace all tiles.
	if (!bComputingClearance && NextTileIndex < TotalTiles)
	{
		const int32 EndTile = FMath::Min(NextTileIndex + TilesPerStep, TotalTiles);
		for (int32 i = NextTileIndex; i < EndTile; ++i)
		{
			ProcessTile(i);
		}
		NextTileIndex = EndTile;

		const float Fraction = 0.85f * (static_cast<float>(NextTileIndex) / FMath::Max(1, TotalTiles));
		UpdateToast(Fraction, FText::FromString(FString::Printf(TEXT("Traces %d / %d tiles"), NextTileIndex, TotalTiles)));
		return true;
	}

	// Phase 2: clearance per layer.
	if (!bComputingClearance)
	{
		bComputingClearance = true;
		ClearanceLayerCursor = 0;

		// Finalize each layer's LayerBaselineZ from the accumulated walkable hit Zs.
		// In Flat mode this is the only Z information the layer carries, so it must
		// reflect the actual floor (not the volume's Min.Z). In HeightOnly / HeightSlope
		// modes the per-cell HeightTier/HeightFixed is stored relative to the baseline,
		// so we also shift those values to maintain the absolute world-Z they encoded.
		for (int32 i = 0; i < BakedAsset->Layers.Num(); ++i)
		{
			if (!LayerHitZCount.IsValidIndex(i) || LayerHitZCount[i] <= 0) { continue; }
			FSeinNavigationLayer& Layer = BakedAsset->Layers[i];
			const float OldBaseline = Layer.LayerBaselineZ;
			const float NewBaseline = static_cast<float>(LayerHitZSum[i] / static_cast<double>(LayerHitZCount[i]));
			if (FMath::IsNearlyEqual(OldBaseline, NewBaseline)) { continue; }

			// Rewrite baseline. For HeightOnly/HeightSlope, also shift per-cell offsets so
			// their decoded world-Z stays put (otherwise cells would appear to jump).
			const float Delta = NewBaseline - OldBaseline;
			Layer.LayerBaselineZ = NewBaseline;

			if (Params.ElevationMode == ESeinElevationMode::HeightOnly)
			{
				for (int32 c = 0; c < Layer.CellsHeight.Num(); ++c)
				{
					const int32 OldTier = Layer.CellsHeight[c].HeightTier;
					const int32 NewTier = FMath::Clamp(FMath::RoundToInt(OldTier - Delta), 0, 255);
					Layer.CellsHeight[c].HeightTier = static_cast<uint8>(NewTier);
				}
			}
			else if (Params.ElevationMode == ESeinElevationMode::HeightSlope)
			{
				for (int32 c = 0; c < Layer.CellsHeightSlope.Num(); ++c)
				{
					const int32 OldFixed = Layer.CellsHeightSlope[c].HeightFixed;
					const int32 NewFixed = FMath::Clamp(FMath::RoundToInt(OldFixed - Delta), -32768, 32767);
					Layer.CellsHeightSlope[c].HeightFixed = static_cast<int16>(NewFixed);
				}
			}
		}

		// Sync the grid-level GridOrigin.Z to layer 0's baseline so callers that
		// read Z via USeinNavigationGrid::GridToWorldCenter (movement profile step
		// goals, generic spatial queries) get the actual ground Z, not the volume
		// bottom. This is the second half of the rebaseline — without it only the
		// debug viz + navlink teleport paths (which route through GetCellWorldZ)
		// would be correct, and movement would still target volume-bottom Z.
		if (BakedAsset->Layers.IsValidIndex(0))
		{
			BakedAsset->GridOrigin.Z = BakedAsset->Layers[0].LayerBaselineZ;
		}
	}

	if (ClearanceLayerCursor < BakedAsset->Layers.Num())
	{
		ComputeClearanceForLayer(ClearanceLayerCursor);
		++ClearanceLayerCursor;

		const float Fraction = 0.75f + 0.1f * (static_cast<float>(ClearanceLayerCursor) / FMath::Max(1, BakedAsset->Layers.Num()));
		UpdateToast(Fraction, FText::FromString(FString::Printf(TEXT("Clearance %d / %d layers"),
			ClearanceLayerCursor, BakedAsset->Layers.Num())));
		return true;
	}

	// Phase 3: abstract graph per layer (cluster nodes + cluster-border edges).
	if (!bBuildingAbstractGraph)
	{
		bBuildingAbstractGraph = true;
		GraphLayerCursor = 0;
		BakedAsset->AbstractGraph.Reset();
	}

	if (GraphLayerCursor < BakedAsset->Layers.Num())
	{
		BuildAbstractGraphForLayer(GraphLayerCursor);
		++GraphLayerCursor;

		const float Fraction = 0.85f + 0.1f * (static_cast<float>(GraphLayerCursor) / FMath::Max(1, BakedAsset->Layers.Num()));
		UpdateToast(Fraction, FText::FromString(FString::Printf(TEXT("Abstract graph %d / %d layers"),
			GraphLayerCursor, BakedAsset->Layers.Num())));
		return true;
	}

	// Phase 4: navlink integration.
	if (!bIntegratingNavLinks)
	{
		bIntegratingNavLinks = true;
		IntegrateNavLinks();
		UpdateToast(0.97f, FText::FromString(FString::Printf(TEXT("NavLinks: %d integrated"), BakedAsset->NavLinks.Num())));
		return true;
	}

	// Phase 5: finalize.
	Finalize();
	ActiveBaker.Reset();
	RemoveFromRoot();
	return false;
}

void USeinNavBaker::ProcessTile(int32 TileIndex)
{
	if (!BakedAsset || BakedAsset->Layers.Num() == 0) { return; }

	const FSeinNavigationLayer& L0 = BakedAsset->Layers[0];
	const int32 TilesX = L0.GetTileWidth();

	const int32 TX = TileIndex % TilesX;
	const int32 TY = TileIndex / TilesX;

	const int32 StartX = TX * Params.TileSize;
	const int32 StartY = TY * Params.TileSize;
	const int32 EndX = FMath::Min(StartX + Params.TileSize, Params.GridWidth);
	const int32 EndY = FMath::Min(StartY + Params.TileSize, Params.GridHeight);

	for (int32 Y = StartY; Y < EndY; ++Y)
	{
		for (int32 X = StartX; X < EndX; ++X)
		{
			TraceCell(X, Y);
		}
	}
}

void USeinNavBaker::TraceCell(int32 WorldX, int32 WorldY)
{
	UWorld* World = WorldPtr.Get();
	if (!World) { return; }

	// Cell center in world XY.
	const float HalfCell = Params.CellSize * 0.5f;
	const FVector CellCenter(
		Params.GridOriginWorld.X + WorldX * Params.CellSize + HalfCell,
		Params.GridOriginWorld.Y + WorldY * Params.CellSize + HalfCell,
		0.0f);

	// Trace from Max.Z down to Min.Z of the volume — geometry must be *inside* the
	// volume to affect nav (matches UE's ANavMeshBoundsVolume semantics).
	const FVector TraceStart(CellCenter.X, CellCenter.Y, Params.UnionWorldBounds.Max.Z);
	const FVector TraceEnd  (CellCenter.X, CellCenter.Y, Params.UnionWorldBounds.Min.Z);

	FCollisionQueryParams QueryParams(TEXT("SeinNavBake"), /*bTraceComplex*/ false);
	QueryParams.bIgnoreTouches = true;
	// Ignore all NavVolumes (they're the bounds container, not geometry).
	for (const TWeakObjectPtr<ASeinNavVolume>& Vol : Volumes)
	{
		if (Vol.IsValid()) { QueryParams.AddIgnoredActor(Vol.Get()); }
	}

	// Gather all up-facing walkable surfaces on the column via iterated single-traces.
	//
	// Why not LineTraceMultiByChannel? UE's multi-trace returns "all overlap hits + the
	// first blocking hit" (see UWorld::LineTraceMultiByChannel doc comment). Every static
	// mesh in a typical level Blocks ECC_WorldStatic, so the multi-trace reports ONE hit
	// (the overpass top) and stops cold — underpass/lower floors are never queried. The
	// trace histogram confirms this as `max-hits-on-any-cell = 1` even over known stacks.
	//
	// Iterated approach: trace to the first blocking hit, record it if it's a walkable
	// surface (nav-relevant + up-facing), add the hit component to the ignore list so it
	// won't block further traces, then retrace from just past the hit point. Repeat until
	// the trace falls off the bottom or no more hits come back. Handles any number of
	// stacked layers (overpass, multi-story buildings, mezzanines) without knowing mesh
	// thicknesses up-front.
	TArray<FHitResult> Hits;
	FCollisionQueryParams IterParams = QueryParams;

	FVector CurStart = TraceStart;
	const int32 MaxHitsPerColumn = 16;  // safety cap; real level geometry rarely stacks >4
	for (int32 Iter = 0; Iter < MaxHitsPerColumn && CurStart.Z > TraceEnd.Z; ++Iter)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, CurStart, TraceEnd, ECC_WorldStatic, IterParams))
		{
			break;
		}

		// Skip anything marked `CanEverAffectNavigation() == false` (same filter UE's
		// NavMesh uses). Skip down-facing hits — they're the exit side of solid meshes;
		// never walking surfaces.
		const UPrimitiveComponent* Comp = Hit.GetComponent();
		const bool bNavRelevant = Comp && Comp->CanEverAffectNavigation();
		const bool bUpFacing    = Hit.ImpactNormal.Z > 0.01f;
		if (bNavRelevant && bUpFacing)
		{
			Hits.Add(Hit);
		}

		// Ignore this component so the next trace passes through to the surface beneath.
		if (Comp) { IterParams.AddIgnoredComponent(Comp); }

		// Nudge the start just past the impact to avoid re-tracing the exact same hit on
		// engines/paths that don't honor the ignore instantly. Tiny offset — doesn't risk
		// skipping legitimately-nearby surfaces because the component is already ignored.
		CurStart = Hit.ImpactPoint - FVector(0.0f, 0.0f, 0.1f);
	}

	// Hit-histogram tallies (post-filter). Surfaces whether multi-layer finds stacks.
	if (Hits.Num() == 0)           { ++CellsWithZeroHits; }
	else if (Hits.Num() == 1)      { ++CellsWithOneHit; }
	else                            { ++CellsWithMultiHits; }
	MaxHitsOnAnyCell = FMath::Max(MaxHitsOnAnyCell, Hits.Num());

	if (Hits.Num() == 0)
	{
		// No geometry under this cell — leave it as default (not walkable).
		// Mark BakeProcessed so debug viz knows layer 0 was visited here even
		// though no surface was found (distinguishes from "never written").
		const int32 Idx = WorldY * Params.GridWidth + WorldX;
		if (BakedAsset->Layers.IsValidIndex(0))
		{
			BakedAsset->Layers[0].SetCellFlag(Idx, SeinCellFlag::Walkable, false);
			BakedAsset->Layers[0].SetCellFlag(Idx, SeinCellFlag::BakeProcessed, true);
			BakedAsset->Layers[0].SetCellMovementCost(Idx, 0);
		}
		return;
	}

	// Sort hits by Z ascending so Layer 0 = lowest walkable surface (ground), higher
	// indices stack upward. Matches DESIGN.md §13: "index 0 is the ground layer, higher
	// indices stack upward."
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.ImpactPoint.Z < B.ImpactPoint.Z; });

	// Translate hits to layers. Flat elevation mode: ground hit only. Height/HeightSlope
	// modes: one layer per hit ≥ LayerSeparation above the last — merges near-coplanar
	// stacks (thin steps, seams) into one layer; keeps distinct floors separate.
	TArray<const FHitResult*> LayerHits;
	if (!Params.IsMultiLayer())
	{
		LayerHits.Add(&Hits[0]);  // ascending sort → [0] is ground
	}
	else
	{
		float LastZ = -TNumericLimits<float>::Max();
		for (const FHitResult& H : Hits)
		{
			if (H.ImpactPoint.Z - LastZ >= Params.LayerSeparation || LayerHits.Num() == 0)
			{
				LayerHits.Add(&H);
				LastZ = H.ImpactPoint.Z;
			}
		}
	}

	// Write per-layer cell state.
	const int32 Idx = WorldY * Params.GridWidth + WorldX;
	for (int32 LayerIndex = 0; LayerIndex < LayerHits.Num(); ++LayerIndex)
	{
		// Ensure layer exists.
		while (!BakedAsset->Layers.IsValidIndex(LayerIndex))
		{
			FSeinNavigationLayer NewLayer;
			NewLayer.LayerBaselineZ = LayerHits[LayerIndex]->ImpactPoint.Z;
			NewLayer.Allocate(Params.GridWidth, Params.GridHeight, Params.ElevationMode, Params.TileSize);
			BakedAsset->Layers.Add(MoveTemp(NewLayer));
			LayerHitZSum.Add(0.0);
			LayerHitZCount.Add(0);
		}

		FSeinNavigationLayer& Layer = BakedAsset->Layers[LayerIndex];
		const FHitResult& Hit = *LayerHits[LayerIndex];

		// Slope check — reject near-vertical surfaces.
		const float UpDot = FVector::DotProduct(Hit.ImpactNormal, FVector::UpVector);
		bool bWalkable = (UpDot >= Params.MaxWalkableSlopeCos);
		uint8 MovementCost = bWalkable ? 1 : 0;

		// Honor nav modifier overrides on the hit primitive's owner.
		if (AActor* HitActor = Hit.GetActor())
		{
			if (USeinNavModifierComponent* Mod = HitActor->FindComponentByClass<USeinNavModifierComponent>())
			{
				switch (Mod->Mode)
				{
				case ESeinNavModifierMode::ForceBlocked:
					bWalkable = false;
					MovementCost = 0;
					break;
				case ESeinNavModifierMode::ForceWalkable:
					bWalkable = true;
					MovementCost = Mod->ForcedMovementCost;
					break;
				case ESeinNavModifierMode::StampTags:
				{
					// Resolve tags through the asset palette (FSeinNavigationLayer has no direct palette reference, so we stash indices inline).
					for (const FGameplayTag& Tag : Mod->StampTags)
					{
						if (!Tag.IsValid()) { continue; }
						int32 PaletteIdx = BakedAsset->CellTagPalette.IndexOfByKey(Tag);
						if (PaletteIdx == INDEX_NONE && BakedAsset->CellTagPalette.Num() < 255)
						{
							PaletteIdx = BakedAsset->CellTagPalette.Add(Tag);
						}
						if (PaletteIdx > 0)
						{
							// Try inline slots first.
							auto TryStamp = [](uint8& A, uint8& B, uint8 Val) -> bool
							{
								if (A == Val || B == Val) { return true; }
								if (A == 0) { A = Val; return true; }
								if (B == 0) { B = Val; return true; }
								return false;
							};
							bool bPlaced = false;
							switch (Layer.ElevationMode)
							{
							case ESeinElevationMode::None:
								if (Layer.CellsFlat.IsValidIndex(Idx)) { bPlaced = TryStamp(Layer.CellsFlat[Idx].TagIndexA, Layer.CellsFlat[Idx].TagIndexB, static_cast<uint8>(PaletteIdx)); }
								break;
							case ESeinElevationMode::HeightOnly:
								if (Layer.CellsHeight.IsValidIndex(Idx)) { bPlaced = TryStamp(Layer.CellsHeight[Idx].TagIndexA, Layer.CellsHeight[Idx].TagIndexB, static_cast<uint8>(PaletteIdx)); }
								break;
							case ESeinElevationMode::HeightSlope:
								if (Layer.CellsHeightSlope.IsValidIndex(Idx)) { bPlaced = TryStamp(Layer.CellsHeightSlope[Idx].TagIndexA, Layer.CellsHeightSlope[Idx].TagIndexB, static_cast<uint8>(PaletteIdx)); }
								break;
							}
							if (!bPlaced)
							{
								FSeinCellTagOverflowEntry& Overflow = Layer.CellTagOverflow.FindOrAdd(Idx);
								if (!Overflow.PaletteIndices.Contains(static_cast<uint8>(PaletteIdx))) { Overflow.PaletteIndices.Add(static_cast<uint8>(PaletteIdx)); }
							}
						}
					}
					break;
				}
				default:
					break;
				}
			}
		}

		// Write walkability + movement cost. BakeProcessed marks this cell as
		// "visited on this layer" so debug viz can distinguish from unvisited
		// default-initialized cells on higher layers (which would otherwise
		// flood-fill red across non-overlap XYs).
		Layer.SetCellFlag(Idx, SeinCellFlag::Walkable, bWalkable);
		Layer.SetCellFlag(Idx, SeinCellFlag::BakeProcessed, true);
		Layer.SetCellMovementCost(Idx, MovementCost);

		// Accumulate walkable hit Z so we can average a sensible baseline for Flat mode.
		if (bWalkable && LayerHitZSum.IsValidIndex(LayerIndex))
		{
			LayerHitZSum[LayerIndex] += Hit.ImpactPoint.Z;
			LayerHitZCount[LayerIndex] += 1;
		}

		// Write height (quantized to fixed-point tier boundary for cross-machine determinism).
		if (Layer.ElevationMode == ESeinElevationMode::HeightOnly && Layer.CellsHeight.IsValidIndex(Idx))
		{
			const float Delta = Hit.ImpactPoint.Z - Layer.LayerBaselineZ;
			const int32 Tier = FMath::Clamp(FMath::RoundToInt(Delta), 0, 255);
			Layer.CellsHeight[Idx].HeightTier = static_cast<uint8>(Tier);
		}
		else if (Layer.ElevationMode == ESeinElevationMode::HeightSlope && Layer.CellsHeightSlope.IsValidIndex(Idx))
		{
			const float Delta = Hit.ImpactPoint.Z - Layer.LayerBaselineZ;
			const int32 Fixed = FMath::Clamp(FMath::RoundToInt(Delta), -32768, 32767);
			Layer.CellsHeightSlope[Idx].HeightFixed = static_cast<int16>(Fixed);

			const int8 SlopeX = static_cast<int8>(FMath::Clamp<int32>(FMath::RoundToInt(Hit.ImpactNormal.X * 127.0f), -127, 127));
			const int8 SlopeY = static_cast<int8>(FMath::Clamp<int32>(FMath::RoundToInt(Hit.ImpactNormal.Y * 127.0f), -127, 127));
			Layer.CellsHeightSlope[Idx].SlopeX = SlopeX;
			Layer.CellsHeightSlope[Idx].SlopeY = SlopeY;
		}
	}
}

void USeinNavBaker::ComputeClearanceForLayer(int32 LayerIdx)
{
	if (!BakedAsset || !BakedAsset->Layers.IsValidIndex(LayerIdx)) { return; }

	FSeinNavigationLayer& Layer = BakedAsset->Layers[LayerIdx];
	const int32 W = Layer.Width;
	const int32 H = Layer.Height;
	Layer.Clearance.SetNumZeroed(W * H);

	// Two-pass Chebyshev-approximate distance transform:
	//   Forward pass (top-left → bottom-right): seed blocked cells as 0, walkable cells
	//   as min(neighbor_dt) + 1 for already-visited neighbors (N, NW, NE, W).
	//   Backward pass (bottom-right → top-left): refine with S, SE, SW, E.
	// Result: per-cell Chebyshev distance to nearest blocked cell, clamped to uint8.

	constexpr uint16 INF = 65000;
	TArray<uint16> DT;
	DT.SetNumUninitialized(W * H);

	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			DT[y * W + x] = Layer.IsCellWalkable(y * W + x) ? INF : 0;
		}
	}

	auto MinU16 = [](uint16 a, uint16 b) { return a < b ? a : b; };

	// Forward pass.
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			const int32 i = y * W + x;
			if (DT[i] == 0) { continue; }
			uint16 best = DT[i];
			if (y > 0)     { best = MinU16(best, DT[i - W] + 1); }
			if (x > 0)     { best = MinU16(best, DT[i - 1] + 1); }
			if (y > 0 && x > 0)         { best = MinU16(best, DT[i - W - 1] + 1); }
			if (y > 0 && x < W - 1)     { best = MinU16(best, DT[i - W + 1] + 1); }
			DT[i] = best;
		}
	}

	// Backward pass.
	for (int32 y = H - 1; y >= 0; --y)
	{
		for (int32 x = W - 1; x >= 0; --x)
		{
			const int32 i = y * W + x;
			if (DT[i] == 0) { continue; }
			uint16 best = DT[i];
			if (y < H - 1)     { best = MinU16(best, DT[i + W] + 1); }
			if (x < W - 1)     { best = MinU16(best, DT[i + 1] + 1); }
			if (y < H - 1 && x < W - 1) { best = MinU16(best, DT[i + W + 1] + 1); }
			if (y < H - 1 && x > 0)     { best = MinU16(best, DT[i + W - 1] + 1); }
			DT[i] = best;
		}
	}

	// Write back to Clearance (clamped to uint8).
	for (int32 i = 0; i < W * H; ++i)
	{
		Layer.Clearance[i] = static_cast<uint8>(FMath::Min<uint16>(DT[i], 255));
	}
}

void USeinNavBaker::BuildAbstractGraphForLayer(int32 LayerIdx)
{
	if (!BakedAsset || !BakedAsset->Layers.IsValidIndex(LayerIdx)) { return; }

	FSeinNavigationLayer& Layer = BakedAsset->Layers[LayerIdx];
	const int32 TilesX = Layer.GetTileWidth();
	const int32 TilesY = Layer.GetTileHeight();
	const int32 TileCount = TilesX * TilesY;

	// Per-cluster "has any walkable cell + representative cell index".
	TArray<int32> ClusterRepresentative;
	ClusterRepresentative.Init(INDEX_NONE, TileCount);

	for (int32 ClusterID = 0; ClusterID < TileCount; ++ClusterID)
	{
		// Stamp the tile's ClusterID field.
		if (Layer.Tiles.IsValidIndex(ClusterID))
		{
			Layer.Tiles[ClusterID].ClusterID = ClusterID;
		}

		const int32 TX = ClusterID % TilesX;
		const int32 TY = ClusterID / TilesX;
		const int32 StartX = TX * Layer.TileSize;
		const int32 StartY = TY * Layer.TileSize;
		const int32 EndX = FMath::Min(StartX + Layer.TileSize, Layer.Width);
		const int32 EndY = FMath::Min(StartY + Layer.TileSize, Layer.Height);

		for (int32 Y = StartY; Y < EndY && ClusterRepresentative[ClusterID] == INDEX_NONE; ++Y)
		{
			for (int32 X = StartX; X < EndX; ++X)
			{
				const int32 Idx = Y * Layer.Width + X;
				if (Layer.IsCellWalkable(Idx))
				{
					ClusterRepresentative[ClusterID] = Idx;
					break;
				}
			}
		}
	}

	// Create one abstract-node per cluster that has a walkable cell.
	TArray<int32> ClusterToNodeIdx;
	ClusterToNodeIdx.Init(INDEX_NONE, TileCount);
	for (int32 ClusterID = 0; ClusterID < TileCount; ++ClusterID)
	{
		if (ClusterRepresentative[ClusterID] == INDEX_NONE) { continue; }

		FSeinAbstractNode Node;
		Node.Layer = static_cast<uint8>(LayerIdx);
		Node.ClusterID = ClusterID;
		Node.RepresentativeCellIndex = ClusterRepresentative[ClusterID];
		ClusterToNodeIdx[ClusterID] = BakedAsset->AbstractGraph.Nodes.Add(Node);
	}

	// Cluster-border edges: for each tile, connect to its east + south neighbors
	// if any walkable cell pair straddles the shared border. (We skip west + north
	// to avoid duplicates; edges are added in both directions below.)
	auto HasWalkableBorder = [&Layer](int32 StartX1, int32 StartY1, int32 EndX1, int32 EndY1,
	                                   int32 StartX2, int32 StartY2, int32 EndX2, int32 EndY2) -> bool
	{
		// We assume one tile is to the east or south of the other. For east neighbor:
		//   tile1 rightmost-column cells vs tile2 leftmost-column cells.
		// For south neighbor:
		//   tile1 bottom-row cells vs tile2 top-row cells.
		// Generalized: walk the shared edge pair and return true on first walkable-pair.
		// Simplified: check a few cells along the border.
		if (StartX2 > StartX1) // east neighbor
		{
			const int32 BorderX1 = EndX1 - 1;
			const int32 BorderX2 = StartX2;
			if (BorderX2 != BorderX1 + 1) { return false; }
			for (int32 Y = FMath::Max(StartY1, StartY2); Y < FMath::Min(EndY1, EndY2); ++Y)
			{
				const int32 Idx1 = Y * Layer.Width + BorderX1;
				const int32 Idx2 = Y * Layer.Width + BorderX2;
				if (Layer.IsCellWalkable(Idx1) && Layer.IsCellWalkable(Idx2))
				{
					return true;
				}
			}
		}
		else if (StartY2 > StartY1) // south neighbor
		{
			const int32 BorderY1 = EndY1 - 1;
			const int32 BorderY2 = StartY2;
			if (BorderY2 != BorderY1 + 1) { return false; }
			for (int32 X = FMath::Max(StartX1, StartX2); X < FMath::Min(EndX1, EndX2); ++X)
			{
				const int32 Idx1 = BorderY1 * Layer.Width + X;
				const int32 Idx2 = BorderY2 * Layer.Width + X;
				if (Layer.IsCellWalkable(Idx1) && Layer.IsCellWalkable(Idx2))
				{
					return true;
				}
			}
		}
		return false;
	};

	auto TileBounds = [&Layer, TilesX](int32 ClusterID, int32& OutStartX, int32& OutStartY, int32& OutEndX, int32& OutEndY)
	{
		const int32 TX = ClusterID % TilesX;
		const int32 TY = ClusterID / TilesX;
		OutStartX = TX * Layer.TileSize;
		OutStartY = TY * Layer.TileSize;
		OutEndX = FMath::Min(OutStartX + Layer.TileSize, Layer.Width);
		OutEndY = FMath::Min(OutStartY + Layer.TileSize, Layer.Height);
	};

	auto AddBorderEdge = [this](int32 FromNode, int32 ToNode, int32 Cost)
	{
		FSeinAbstractEdge Fwd;
		Fwd.FromNode = FromNode;
		Fwd.ToNode = ToNode;
		Fwd.Cost = Cost;
		Fwd.EdgeType = ESeinAbstractEdgeType::ClusterBorder;
		Fwd.bEnabled = true;
		const int32 FwdIdx = BakedAsset->AbstractGraph.Edges.Add(Fwd);
		BakedAsset->AbstractGraph.Nodes[FromNode].OutgoingEdgeIndices.Add(FwdIdx);

		FSeinAbstractEdge Rev;
		Rev.FromNode = ToNode;
		Rev.ToNode = FromNode;
		Rev.Cost = Cost;
		Rev.EdgeType = ESeinAbstractEdgeType::ClusterBorder;
		Rev.bEnabled = true;
		const int32 RevIdx = BakedAsset->AbstractGraph.Edges.Add(Rev);
		BakedAsset->AbstractGraph.Nodes[ToNode].OutgoingEdgeIndices.Add(RevIdx);
	};

	for (int32 ClusterID = 0; ClusterID < TileCount; ++ClusterID)
	{
		const int32 FromNodeIdx = ClusterToNodeIdx[ClusterID];
		if (FromNodeIdx == INDEX_NONE) { continue; }

		int32 SX1, SY1, EX1, EY1;
		TileBounds(ClusterID, SX1, SY1, EX1, EY1);

		const int32 TX = ClusterID % TilesX;
		const int32 TY = ClusterID / TilesX;

		// East neighbor.
		if (TX + 1 < TilesX)
		{
			const int32 EastCluster = ClusterID + 1;
			const int32 EastNodeIdx = ClusterToNodeIdx[EastCluster];
			if (EastNodeIdx != INDEX_NONE)
			{
				int32 SX2, SY2, EX2, EY2;
				TileBounds(EastCluster, SX2, SY2, EX2, EY2);
				if (HasWalkableBorder(SX1, SY1, EX1, EY1, SX2, SY2, EX2, EY2))
				{
					AddBorderEdge(FromNodeIdx, EastNodeIdx, Layer.TileSize); // cost ~ one tile width
				}
			}
		}

		// South neighbor.
		if (TY + 1 < TilesY)
		{
			const int32 SouthCluster = ClusterID + TilesX;
			const int32 SouthNodeIdx = ClusterToNodeIdx[SouthCluster];
			if (SouthNodeIdx != INDEX_NONE)
			{
				int32 SX2, SY2, EX2, EY2;
				TileBounds(SouthCluster, SX2, SY2, EX2, EY2);
				if (HasWalkableBorder(SX1, SY1, EX1, EY1, SX2, SY2, EX2, EY2))
				{
					AddBorderEdge(FromNodeIdx, SouthNodeIdx, Layer.TileSize);
				}
			}
		}
	}
}

static void StampTerrainVolumeIntoAsset(ASeinTerrainVolume* Vol, USeinNavigationGridAsset* Asset)
{
	if (!Vol || !Asset || Asset->CellSize <= 0.0f) { return; }
	if (Vol->CoverTags.IsEmpty() && Vol->MovementCostOverride == 0) { return; }

	const FBox Bounds = Vol->GetVolumeWorldBounds();
	if (!Bounds.IsValid) { return; }

	const float CellSize = Asset->CellSize;
	const int32 MinX = FMath::Max(0, FMath::FloorToInt((Bounds.Min.X - Asset->GridOrigin.X) / CellSize));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt((Bounds.Min.Y - Asset->GridOrigin.Y) / CellSize));
	const int32 MaxX = FMath::Min(Asset->GridWidth - 1, FMath::CeilToInt((Bounds.Max.X - Asset->GridOrigin.X) / CellSize));
	const int32 MaxY = FMath::Min(Asset->GridHeight - 1, FMath::CeilToInt((Bounds.Max.Y - Asset->GridOrigin.Y) / CellSize));

	// Resolve each tag to a palette index (append if new).
	TArray<uint8> PaletteIndices;
	PaletteIndices.Reserve(Vol->CoverTags.Num());
	for (const FGameplayTag& Tag : Vol->CoverTags)
	{
		if (!Tag.IsValid()) { continue; }
		int32 Idx = Asset->CellTagPalette.IndexOfByKey(Tag);
		if (Idx == INDEX_NONE && Asset->CellTagPalette.Num() < 255)
		{
			Idx = Asset->CellTagPalette.Add(Tag);
		}
		if (Idx > 0) { PaletteIndices.Add(static_cast<uint8>(Idx)); }
	}

	// Stamp layer 0 (ground) — multi-layer terrain stamping is a polish follow-up.
	const int32 LayerIdx = 0;
	if (!Asset->Layers.IsValidIndex(LayerIdx)) { return; }
	FSeinNavigationLayer& L = Asset->Layers[LayerIdx];

	auto StampTag = [](FSeinNavigationLayer& Layer, int32 CellIdx, uint8 PaletteIdx)
	{
		auto TryStamp = [PaletteIdx](uint8& A, uint8& B) -> bool
		{
			if (A == PaletteIdx || B == PaletteIdx) { return true; }
			if (A == 0) { A = PaletteIdx; return true; }
			if (B == 0) { B = PaletteIdx; return true; }
			return false;
		};
		bool bPlaced = false;
		switch (Layer.ElevationMode)
		{
		case ESeinElevationMode::None:
			if (Layer.CellsFlat.IsValidIndex(CellIdx)) { bPlaced = TryStamp(Layer.CellsFlat[CellIdx].TagIndexA, Layer.CellsFlat[CellIdx].TagIndexB); }
			break;
		case ESeinElevationMode::HeightOnly:
			if (Layer.CellsHeight.IsValidIndex(CellIdx)) { bPlaced = TryStamp(Layer.CellsHeight[CellIdx].TagIndexA, Layer.CellsHeight[CellIdx].TagIndexB); }
			break;
		case ESeinElevationMode::HeightSlope:
			if (Layer.CellsHeightSlope.IsValidIndex(CellIdx)) { bPlaced = TryStamp(Layer.CellsHeightSlope[CellIdx].TagIndexA, Layer.CellsHeightSlope[CellIdx].TagIndexB); }
			break;
		}
		if (!bPlaced)
		{
			FSeinCellTagOverflowEntry& Overflow = Layer.CellTagOverflow.FindOrAdd(CellIdx);
			if (!Overflow.PaletteIndices.Contains(PaletteIdx)) { Overflow.PaletteIndices.Add(PaletteIdx); }
		}
	};

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const int32 Idx = Y * Asset->GridWidth + X;
			for (uint8 PaletteIdx : PaletteIndices)
			{
				StampTag(L, Idx, PaletteIdx);
			}
			if (Vol->MovementCostOverride > 0)
			{
				L.SetCellMovementCost(Idx, Vol->MovementCostOverride);
			}
		}
	}
}

void USeinNavBaker::IntegrateNavLinks()
{
	UWorld* World = WorldPtr.Get();
	if (!World || !BakedAsset) { return; }

	// Stamp terrain volumes first so navlinks can reference tags that land next.
	int32 VolumeCount = 0;
	for (TActorIterator<ASeinTerrainVolume> It(World); It; ++It)
	{
		StampTerrainVolumeIntoAsset(*It, BakedAsset);
		++VolumeCount;
	}
	if (VolumeCount > 0)
	{
		UE_LOG(LogSeinNavBake, Log, TEXT("Stamped %d terrain volume(s) into the asset"), VolumeCount);
	}

	// Temporary "scratch" grid for cluster resolution — the baked-asset fields mirror
	// what we need, so we use BakedAsset directly. Endpoint → FSeinCellAddress resolution
	// uses the asset's grid metadata.

	for (TActorIterator<ASeinNavLinkProxy> It(World); It; ++It)
	{
		ASeinNavLinkProxy* Proxy = *It;
		if (!Proxy) { continue; }

		const FVector StartPos = Proxy->GetStartWorldTransform().GetLocation();
		const FVector EndPos = Proxy->GetEndWorldTransform().GetLocation();

		// Skip links whose endpoints fall outside the baked bounds.
		if (!Params.UnionWorldBounds.IsInsideOrOn(StartPos) || !Params.UnionWorldBounds.IsInsideOrOn(EndPos))
		{
			UE_LOG(LogSeinNavBake, Warning, TEXT("Skipping NavLink '%s' — endpoints outside baked bounds"),
				*Proxy->GetName());
			continue;
		}

		// Resolve endpoints to (layer, cell).
		auto ResolveEndpoint = [this](const FVector& WorldPos) -> FSeinCellAddress
		{
			const int32 CellX = FMath::FloorToInt((WorldPos.X - Params.GridOriginWorld.X) / Params.CellSize);
			const int32 CellY = FMath::FloorToInt((WorldPos.Y - Params.GridOriginWorld.Y) / Params.CellSize);
			if (CellX < 0 || CellX >= Params.GridWidth || CellY < 0 || CellY >= Params.GridHeight)
			{
				return FSeinCellAddress::Invalid();
			}
			const int32 Idx = CellY * Params.GridWidth + CellX;

			// Pick layer with highest baseline that is <= WorldPos.Z.
			int32 BestLayer = 0;
			float BestZ = -TNumericLimits<float>::Max();
			for (int32 i = 0; i < BakedAsset->Layers.Num(); ++i)
			{
				const float BZ = BakedAsset->Layers[i].LayerBaselineZ;
				if (BZ <= WorldPos.Z && BZ > BestZ)
				{
					BestZ = BZ;
					BestLayer = i;
				}
			}
			return FSeinCellAddress(static_cast<uint8>(BestLayer), Idx);
		};

		const FSeinCellAddress StartCell = ResolveEndpoint(StartPos);
		const FSeinCellAddress EndCell = ResolveEndpoint(EndPos);
		if (!StartCell.IsValid() || !EndCell.IsValid())
		{
			UE_LOG(LogSeinNavBake, Warning, TEXT("Skipping NavLink '%s' — endpoint did not resolve to a valid cell"),
				*Proxy->GetName());
			continue;
		}

		// Find abstract-nodes for each endpoint's cluster.
		auto ClusterForCell = [this](const FSeinCellAddress& Addr) -> int32
		{
			if (!BakedAsset->Layers.IsValidIndex(Addr.Layer)) { return INDEX_NONE; }
			const FSeinNavigationLayer& L = BakedAsset->Layers[Addr.Layer];
			const int32 CellX = Addr.CellIndex % L.Width;
			const int32 CellY = Addr.CellIndex / L.Width;
			return (CellY / L.TileSize) * L.GetTileWidth() + (CellX / L.TileSize);
		};

		const int32 StartCluster = ClusterForCell(StartCell);
		const int32 EndCluster = ClusterForCell(EndCell);

		const int32 StartNodeIdx = BakedAsset->AbstractGraph.FindNode(StartCell.Layer, StartCluster);
		const int32 EndNodeIdx = BakedAsset->AbstractGraph.FindNode(EndCell.Layer, EndCluster);

		// If either endpoint's cluster has no abstract-node (no walkable cells), create one.
		auto EnsureNode = [this](const FSeinCellAddress& Addr, int32 Cluster) -> int32
		{
			const int32 Existing = BakedAsset->AbstractGraph.FindNode(Addr.Layer, Cluster);
			if (Existing != INDEX_NONE) { return Existing; }
			FSeinAbstractNode Node;
			Node.Layer = Addr.Layer;
			Node.ClusterID = Cluster;
			Node.RepresentativeCellIndex = Addr.CellIndex;
			return BakedAsset->AbstractGraph.Nodes.Add(Node);
		};

		const int32 SrcNode = EnsureNode(StartCell, StartCluster);
		const int32 DstNode = EnsureNode(EndCell, EndCluster);

		// Create the navlink record.
		FSeinNavLinkRecord Rec;
		Rec.StartCell = StartCell;
		Rec.EndCell = EndCell;
		Rec.AdditionalCost = Proxy->AdditionalCost;
		Rec.bBidirectional = Proxy->bBidirectional;
		Rec.EligibilityQuery = Proxy->EligibilityQuery;
		Rec.TraversalAbility = Proxy->TraversalAbility;
		Rec.SourceActorPath = FSoftObjectPath(Proxy);
		Rec.bEnabled = true;

		const int32 BaseCost = FMath::Max(1, FMath::CeilToInt(FVector::Dist(StartPos, EndPos) / FMath::Max(1.0f, Params.CellSize)));
		const int32 TotalCost = BaseCost + Proxy->AdditionalCost;

		// Forward edge.
		{
			FSeinAbstractEdge E;
			E.FromNode = SrcNode;
			E.ToNode = DstNode;
			E.Cost = TotalCost;
			E.EdgeType = ESeinAbstractEdgeType::NavLink;
			E.TraversalAbility = Proxy->TraversalAbility;
			E.EligibilityQuery = Proxy->EligibilityQuery;
			E.NavLinkID = BakedAsset->NavLinks.Num();
			E.bEnabled = true;
			const int32 EdgeIdx = BakedAsset->AbstractGraph.Edges.Add(E);
			BakedAsset->AbstractGraph.Nodes[SrcNode].OutgoingEdgeIndices.Add(EdgeIdx);
			Rec.EdgeIndices.Add(EdgeIdx);
		}

		// Reverse edge if bidirectional.
		if (Proxy->bBidirectional)
		{
			FSeinAbstractEdge E;
			E.FromNode = DstNode;
			E.ToNode = SrcNode;
			E.Cost = TotalCost;
			E.EdgeType = ESeinAbstractEdgeType::NavLink;
			E.TraversalAbility = Proxy->TraversalAbility;
			E.EligibilityQuery = Proxy->EligibilityQuery;
			E.NavLinkID = BakedAsset->NavLinks.Num();
			E.bEnabled = true;
			const int32 EdgeIdx = BakedAsset->AbstractGraph.Edges.Add(E);
			BakedAsset->AbstractGraph.Nodes[DstNode].OutgoingEdgeIndices.Add(EdgeIdx);
			Rec.EdgeIndices.Add(EdgeIdx);
		}

		BakedAsset->NavLinks.Add(Rec);
	}

	UE_LOG(LogSeinNavBake, Log, TEXT("Integrated %d navlink(s) into the abstract graph"),
		BakedAsset->NavLinks.Num());
}

void USeinNavBaker::Finalize()
{
	if (!BakedAsset) { return; }

	// Metadata.
	BakedAsset->BakeTimestamp = FDateTime::UtcNow();
	BakedAsset->BakeMachine = FPlatformProcess::ComputerName();
	BakedAsset->BakeCellCount = Params.GridWidth * Params.GridHeight * BakedAsset->Layers.Num();

#if WITH_EDITOR
	// Save the asset as a companion uasset in the level's folder.
	if (UWorld* World = WorldPtr.Get())
	{
		const FString LevelPackagePath = World->GetOutermost()->GetName();
		const FString LevelDir = FPackageName::GetLongPackagePath(LevelPackagePath);
		const FString LevelName = FPackageName::GetShortName(LevelPackagePath);
		const FString AssetName = FString::Printf(TEXT("NavGrid_%s"), *LevelName);
		const FString AssetPath = FString::Printf(TEXT("%s/%s"), *LevelDir, *AssetName);

		UPackage* Pkg = CreatePackage(*AssetPath);
		if (Pkg)
		{
			Pkg->FullyLoad();
			USeinNavigationGridAsset* Persisted = NewObject<USeinNavigationGridAsset>(Pkg, *AssetName, RF_Public | RF_Standalone);
			Persisted->GridWidth = BakedAsset->GridWidth;
			Persisted->GridHeight = BakedAsset->GridHeight;
			Persisted->CellSize = BakedAsset->CellSize;
			Persisted->GridOrigin = BakedAsset->GridOrigin;
			Persisted->TileSize = BakedAsset->TileSize;
			Persisted->ElevationMode = BakedAsset->ElevationMode;
			Persisted->CellTagPalette = BakedAsset->CellTagPalette;
			Persisted->Layers = BakedAsset->Layers;
			Persisted->AbstractGraph = BakedAsset->AbstractGraph;
			Persisted->NavLinks = BakedAsset->NavLinks;
			Persisted->BakeTimestamp = BakedAsset->BakeTimestamp;
			Persisted->BakeMachine = BakedAsset->BakeMachine;
			Persisted->BakeCellCount = BakedAsset->BakeCellCount;
			Persisted->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(Persisted);

			// Point all volumes at the persisted asset + kick their debug viz to rebuild
			// against the fresh data. Without the ForceUpdate, a bake that finishes while
			// `P` is already on would keep rendering the stale pre-bake proxy until the
			// user toggles the flag off/on.
			for (const TWeakObjectPtr<ASeinNavVolume>& Vol : Volumes)
			{
				if (ASeinNavVolume* V = Vol.Get())
				{
					V->BakedGridAsset = Persisted;
					V->MarkPackageDirty();
#if UE_ENABLE_DEBUG_DRAWING
					if (V->DebugRenderComponent) { V->DebugRenderComponent->ForceUpdate(); }
#endif
				}
			}

			BakedAsset = Persisted;
		}
	}
#endif

	const FText SuccessMsg = FText::FromString(FString::Printf(
		TEXT("SeinARTS nav baked: %d×%d cells, %d layers"),
		Params.GridWidth, Params.GridHeight, BakedAsset->Layers.Num()));
	DismissToast(true, SuccessMsg);

	UE_LOG(LogSeinNavBake, Log, TEXT("Bake complete: %d cells total"), BakedAsset->BakeCellCount);

	// Hit histogram — tells us whether the column traces found stacked geometry.
	// If CellsWithMultiHits is 0, multi-layer has nothing to stack — almost always
	// means the nav volume's Min.Z/Max.Z doesn't span both floors, or the geometry
	// is flagged `CanEverAffectNavigation()=false`.
	UE_LOG(LogSeinNavBake, Log,
		TEXT("Trace histogram: 0 hits=%d, 1 hit=%d, 2+ hits=%d (max on any cell=%d)"),
		CellsWithZeroHits, CellsWithOneHit, CellsWithMultiHits, MaxHitsOnAnyCell);

	// Per-layer diagnostic — surfaces whether multi-layer actually produced
	// more than one layer and how many walkable cells landed on each.
	for (int32 i = 0; i < BakedAsset->Layers.Num(); ++i)
	{
		const FSeinNavigationLayer& L = BakedAsset->Layers[i];
		int32 Walkable = 0;
		int32 Processed = 0;
		const int32 Total = L.Width * L.Height;
		for (int32 c = 0; c < Total; ++c)
		{
			const uint8 Flags = L.GetCellFlags(c);
			if (Flags & SeinCellFlag::BakeProcessed) { ++Processed; }
			if ((Flags & SeinCellFlag::Walkable) && !(Flags & SeinCellFlag::DynamicBlocked)) { ++Walkable; }
		}
		UE_LOG(LogSeinNavBake, Log,
			TEXT("  Layer %d: baseline Z=%.1f, %d/%d cells processed, %d walkable"),
			i, L.LayerBaselineZ, Processed, Total, Walkable);
	}
}

// ---------------- Toast helpers ----------------

void USeinNavBaker::ShowToast(const FText& Message)
{
#if WITH_EDITOR
	FNotificationInfo Info(Message);
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.ExpireDuration = 0.0f;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		NSLOCTEXT("SeinARTSNavigation", "CancelBake", "Cancel"),
		NSLOCTEXT("SeinARTSNavigation", "CancelBakeTooltip", "Cancel the in-progress nav bake"),
		FSimpleDelegate::CreateWeakLambda(this, [this]() { RequestCancel(); })));

	NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}
#endif
}

void USeinNavBaker::UpdateToast(float Fraction, const FText& StatusLine)
{
#if WITH_EDITOR
	if (!NotificationItem.IsValid()) { return; }
	const FText Combined = FText::FromString(FString::Printf(TEXT("Baking SeinARTS nav — %.0f%%  (%s)"),
		FMath::Clamp(Fraction * 100.0f, 0.0f, 100.0f), *StatusLine.ToString()));
	NotificationItem->SetText(Combined);
#endif
}

void USeinNavBaker::DismissToast(bool bSuccess, const FText& FinalText)
{
#if WITH_EDITOR
	if (!NotificationItem.IsValid()) { return; }
	NotificationItem->SetText(FinalText);
	NotificationItem->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_None);
	NotificationItem->ExpireAndFadeout();
	NotificationItem.Reset();
#endif
}
