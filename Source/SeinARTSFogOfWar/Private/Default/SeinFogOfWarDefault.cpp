/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.cpp
 * @brief   MVP stamp engine + sync bake. Flat-circle stamp (shadowcast is
 *          step 2); bake runs a two-trace-per-cell pass to detect static
 *          blockers above terrain and serializes quantized uint8 heights
 *          into a USeinFogOfWarDefaultAsset. Runtime dequantizes to
 *          FFixedPoint on load.
 */

#include "Default/SeinFogOfWarDefault.h"
#include "Default/SeinFogOfWarDefaultAsset.h"
#include "Volumes/SeinFogOfWarVolume.h"
#include "Components/SeinVisionData.h"
#include "Components/SeinExtentsData.h"
#include "Stamping/SeinStampShape.h"
#include "Stamping/SeinStampUtils.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"

#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Components/ActorComponents/SeinMovementComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Entity.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Math/Box.h"
#include "Misc/ScopedSlowTask.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSeinFogOfWarDefault, Log, All);

// ============================================================================
// Asset class
// ============================================================================

TSubclassOf<USeinFogOfWarAsset> USeinFogOfWarDefault::GetAssetClass() const
{
	return USeinFogOfWarDefaultAsset::StaticClass();
}

// ============================================================================
// Bake
// ============================================================================

bool USeinFogOfWarDefault::BeginBake(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("BeginBake: null world"));
		return false;
	}
	if (bBaking)
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("BeginBake: already baking"));
		return false;
	}

	bBaking = true;
	bCancelRequested = false;
	ON_SCOPE_EXIT { bBaking = false; bCancelRequested = false; };

	USeinFogOfWarDefaultAsset* NewAsset = nullptr;
	const bool bOk = DoSyncBake(World, NewAsset);
	if (!bOk || !NewAsset)
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("BeginBake: failed"));
		return false;
	}

	// Point every fog volume at the new asset + load into this impl.
	for (TActorIterator<ASeinFogOfWarVolume> It(World); It; ++It)
	{
		It->BakedAsset = NewAsset;
		It->MarkPackageDirty();
	}
	LoadFromAsset(NewAsset);

	UE_LOG(LogSeinFogOfWarDefault, Log,
		TEXT("Bake complete: %dx%d cells, CellSize=%s"),
		NewAsset->Width, NewAsset->Height, *NewAsset->CellSize.ToString());
	return true;
}

bool USeinFogOfWarDefault::DoSyncBake(UWorld* World, USeinFogOfWarDefaultAsset*& OutAsset)
{
	OutAsset = nullptr;

	// Gather volumes + union their bounds.
	TArray<ASeinFogOfWarVolume*> Volumes;
	FBox UnionBounds(ForceInit);
	for (TActorIterator<ASeinFogOfWarVolume> It(World); It; ++It)
	{
		ASeinFogOfWarVolume* Vol = *It;
		if (!Vol) continue;
		Volumes.Add(Vol);
		UnionBounds += Vol->GetVolumeWorldBounds();
	}
	if (Volumes.Num() == 0 || !UnionBounds.IsValid)
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("DoSyncBake: no FogOfWarVolumes in world"));
		return false;
	}

	// Resolve bake parameters from the first volume (MVP uses single global
	// cell size; later volumes use their own for their region once bake
	// supports per-region grids). Getter returns FFixedPoint directly — the
	// only float touched here is the bake-time trace-math float, computed
	// once via ToFloat().
	const FFixedPoint BakedCellSize = Volumes[0]->GetResolvedCellSize();
	const float CellSizeF = BakedCellSize.ToFloat();
	const bool bBakeStaticBlockers = Volumes[0]->bBakeStaticBlockers;

	const int32 GridW = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.X - UnionBounds.Min.X) / CellSizeF));
	const int32 GridH = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.Y - UnionBounds.Min.Y) / CellSizeF));
	const FVector OriginWorld(UnionBounds.Min.X, UnionBounds.Min.Y, UnionBounds.Min.Z);
	const float TopZ = UnionBounds.Max.Z + BakeTraceHeadroom;
	const float BottomZ = UnionBounds.Min.Z - 100.0f;

	// Height quantization spans the volume's Z range over 255 steps (uint8).
	// A +1 nudge on MinHeight prevents the highest-Z cell from rounding to
	// 256 (out of uint8 range) at the top edge.
	const float RangeZ = FMath::Max(1.0f, UnionBounds.Max.Z - UnionBounds.Min.Z + BakeTraceHeadroom);
	const float QuantumF = FMath::Max(1.0f, RangeZ / 255.0f);

#if WITH_EDITOR
	FScopedSlowTask Task(GridW * GridH, NSLOCTEXT("SeinFogOfWarDefault", "Baking", "Baking SeinARTS Fog Of War..."));
	Task.MakeDialog(/*bShowCancelButton*/ true);
#endif

	// Create asset (editor) or transient package (runtime bake).
#if WITH_EDITOR
	const FString AssetName = FString::Printf(TEXT("FogOfWarData_%s"), *World->GetMapName());
	OutAsset = CreateOrLoadAsset(World, AssetName);
#else
	OutAsset = NewObject<USeinFogOfWarDefaultAsset>(GetTransientPackage());
#endif
	if (!OutAsset) return false;

	OutAsset->Width = GridW;
	OutAsset->Height = GridH;
	OutAsset->CellSize = BakedCellSize;
	OutAsset->Origin = FFixedVector(FFixedPoint::FromFloat(OriginWorld.X),
	                                FFixedPoint::FromFloat(OriginWorld.Y),
	                                FFixedPoint::FromFloat(OriginWorld.Z));
	OutAsset->MinHeight = FFixedPoint::FromFloat(OriginWorld.Z);
	OutAsset->HeightQuantum = FFixedPoint::FromFloat(QuantumF);
	OutAsset->Cells.SetNum(GridW * GridH);

	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinFogOfWarBake), /*bTraceComplex*/ true);
	for (ASeinFogOfWarVolume* Vol : Volumes)
	{
		if (Vol) QP.AddIgnoredActor(Vol);
	}

	// Per-archetype skip list. Mirrors the nav-bake pattern. Two reasons to
	// exclude an ASeinActor's geometry from the static fog bake:
	//   1) Designer flagged the archetype as `bBakesIntoFogOfWar = false` —
	//      glass walls, transparent props, anything that should NOT occlude
	//      sight even though it may participate in nav.
	//   2) Mobile heuristic — the archetype CDO carries a USeinMovementComponent.
	//      Vehicles, infantry, and any other unit running movement should
	//      not carve their pose-at-bake-time into the static fog grid.
	// `QP.AddIgnoredActor` ignores every primitive on the actor (skeletal
	// meshes, collision capsules, brush components — the lot), so the bake
	// trace passes through cleanly regardless of how the BP is composed.
	int32 NumIgnoredActors = 0;
	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* SeinActor = *It;
		if (!SeinActor) continue;

		bool bSkip = false;

		if (const USeinArchetypeDefinition* ArchDef = SeinActor->ArchetypeDefinition)
		{
			if (!ArchDef->bBakesIntoFogOfWar)
			{
				bSkip = true;
			}
		}

		if (!bSkip && SeinActor->FindComponentByClass<USeinMovementComponent>())
		{
			bSkip = true;
		}

		if (bSkip)
		{
			QP.AddIgnoredActor(SeinActor);
			++NumIgnoredActors;
		}
	}
	UE_LOG(LogSeinFogOfWarDefault, Log,
		TEXT("Bake: ignoring %d SeinActor(s) (mobile + bBakesIntoFogOfWar=false)"),
		NumIgnoredActors);

	// Cell-footprint box shape for the top trace. Half-extents (cellHalf,
	// cellHalf, 1cm). Using a box sweep here — not a line trace — so thin
	// walls / hedgerows / fences that don't intersect the cell center still
	// register as the cell's top surface. A line trace would miss them
	// entirely, leaving thin blockers invisible to LOS.
	const FCollisionShape CellBox = FCollisionShape::MakeBox(
		FVector(CellSizeF * 0.5f, CellSizeF * 0.5f, 1.0f));

	int32 BakeGroundHits = 0;
	int32 BakeBlockerHits = 0;
	int32 BakeMisses = 0;
	int32 Processed = 0;

	for (int32 Y = 0; Y < GridH; ++Y)
	{
		for (int32 X = 0; X < GridW; ++X)
		{
			if (bCancelRequested)
			{
				UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("Bake cancelled by user"));
				OutAsset = nullptr;
				return false;
			}

			const float CX = OriginWorld.X + (X + 0.5f) * CellSizeF;
			const float CY = OriginWorld.Y + (Y + 0.5f) * CellSizeF;
			const FVector Start(CX, CY, TopZ);
			const FVector End  (CX, CY, BottomZ);

			FSeinFogOfWarCell& Cell = OutAsset->Cells[Y * GridW + X];

			// Trace 1: box sweep for the topmost opaque surface anywhere in
			// this cell's footprint (catches thin blockers).
			FHitResult TopHit;
			if (!World->SweepSingleByChannel(TopHit, Start, End, FQuat::Identity, BakeTraceChannel, CellBox, QP))
			{
				// No geometry at all — no ground, no blocker. GroundHeight
				// defaults to the volume's bottom (quantized 0).
				Cell.GroundHeight = 0;
				Cell.BlockerHeight = 0;
				Cell.BlockerLayerMask = 0;
				++BakeMisses;
			}
			else
			{
				const float TopHitZ = TopHit.ImpactPoint.Z;
				float GroundZ = TopHitZ;
				float BlockerRelZ = 0.0f;

				if (bBakeStaticBlockers)
				{
					// Trace 2: from the top hit downward, ignoring the top
					// actor, for the ground beneath. If we miss, the top hit
					// IS the ground (simple terrain). If we hit, the gap is
					// a static blocker above the ground.
					FCollisionQueryParams QP2 = QP;
					if (AActor* TopActor = TopHit.GetActor())
					{
						QP2.AddIgnoredActor(TopActor);
					}

					FHitResult GroundHit;
					const FVector Trace2Start(CX, CY, TopHitZ - 0.1f);
					if (World->LineTraceSingleByChannel(GroundHit, Trace2Start, End, BakeTraceChannel, QP2))
					{
						const float GroundHitZ = GroundHit.ImpactPoint.Z;
						const float Gap = TopHitZ - GroundHitZ;
						if (Gap >= StaticBlockerMinHeight)
						{
							GroundZ = GroundHitZ;
							BlockerRelZ = Gap;
							++BakeBlockerHits;
						}
						// else: top hit is walkable (terrain noise gap is negligible)
					}
				}
				// else: static blocker pass disabled — the top hit is whatever
				// stands at the top of this cell and we treat it as ground;
				// no blocker contribution. All sight occlusion from this point
				// on comes from runtime USeinExtentsComponent (bBlocksFogOfWar) overlays.

				// Quantize. Ground stored relative to MinHeight; Blocker
				// stored as height above Ground (both uint8).
				const float GroundStepsF = (GroundZ - OriginWorld.Z) / QuantumF;
				const float BlockerStepsF = BlockerRelZ / QuantumF;
				Cell.GroundHeight = (uint8)FMath::Clamp(FMath::RoundToInt(GroundStepsF), 0, 255);
				Cell.BlockerHeight = (uint8)FMath::Clamp(FMath::RoundToInt(BlockerStepsF), 0, 255);
				Cell.BlockerLayerMask = (BlockerRelZ >= StaticBlockerMinHeight) ? SEIN_FOW_MASK_VISIBLE : 0;
				++BakeGroundHits;
			}

			++Processed;
#if WITH_EDITOR
			if ((Processed & 255) == 0)
			{
				Task.EnterProgressFrame(256.0f);
				if (Task.ShouldCancel())
				{
					bCancelRequested = true;
				}
			}
#endif
		}
	}

	UE_LOG(LogSeinFogOfWarDefault, Log,
		TEXT("Bake stats: %dx%d=%d cells — ground=%d blockers=%d no_hit=%d"),
		GridW, GridH, GridW * GridH, BakeGroundHits, BakeBlockerHits, BakeMisses);

#if WITH_EDITOR
	if (!SaveAssetToDisk(OutAsset))
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning, TEXT("Bake: failed to save asset to disk"));
		// Asset still usable in-memory this session.
	}
#endif

	return true;
}

#if WITH_EDITOR
USeinFogOfWarDefaultAsset* USeinFogOfWarDefault::CreateOrLoadAsset(UWorld* World, const FString& AssetName) const
{
	const FString PackagePath = FString::Printf(TEXT("/Game/FogOfWarData/%s"), *AssetName);
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) return nullptr;

	USeinFogOfWarDefaultAsset* Existing = FindObject<USeinFogOfWarDefaultAsset>(Package, *AssetName);
	if (Existing) return Existing;

	USeinFogOfWarDefaultAsset* Asset = NewObject<USeinFogOfWarDefaultAsset>(
		Package, USeinFogOfWarDefaultAsset::StaticClass(), FName(*AssetName),
		RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Asset);
	return Asset;
}

bool USeinFogOfWarDefault::SaveAssetToDisk(USeinFogOfWarDefaultAsset* Asset) const
{
	if (!Asset) return false;
	UPackage* Pkg = Asset->GetOutermost();
	if (!Pkg) return false;

	Pkg->MarkPackageDirty();

	const FString Filename = FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_None;
	Args.Error = GError;
	return UPackage::SavePackage(Pkg, Asset, *Filename, Args);
}
#endif

// ============================================================================
// Load
// ============================================================================

void USeinFogOfWarDefault::LoadFromAsset(USeinFogOfWarAsset* Asset)
{
	Super::LoadFromAsset(Asset);
	if (const USeinFogOfWarDefaultAsset* Concrete = Cast<USeinFogOfWarDefaultAsset>(Asset))
	{
		ApplyAssetData(Concrete);
	}
	else
	{
		Width = 0;
		Height = 0;
		GroundHeight.Reset();
		BlockerHeight.Reset();
		BlockerLayerMask.Reset();
		DynamicBlockerHeight.Reset();
		DynamicBlockerLayerMask.Reset();
		VisionGroups.Empty();
		SourceStates.Empty();
		LastDynamicBlockerHash = 0;
	}
	OnFogOfWarMutated.Broadcast();
}

void USeinFogOfWarDefault::ApplyAssetData(const USeinFogOfWarDefaultAsset* Asset)
{
	if (!Asset)
	{
		Width = 0;
		Height = 0;
		return;
	}

	Width = Asset->Width;
	Height = Asset->Height;
	CellSize = Asset->CellSize;
	Origin = Asset->Origin;

	const int32 NumCells = Width * Height;
	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumUninitialized(NumCells);
	BlockerLayerMask.SetNumUninitialized(NumCells);
	DynamicBlockerHeight.SetNumZeroed(NumCells);
	DynamicBlockerLayerMask.SetNumZeroed(NumCells);
	VisionGroups.Empty(); // per-player state recreates lazily on next stamp
	SourceStates.Empty();
	LastDynamicBlockerHash = 0;

	// Dequantize uint8 heights → FFixedPoint. Runtime stores ABSOLUTE world Z
	// for both (Ground = MinHeight + Q·steps; Blocker = Ground + Q·steps) so
	// shadowcast's lampshade test is a straight world-Z compare.
	const FFixedPoint MinH = Asset->MinHeight;
	const FFixedPoint Q = Asset->HeightQuantum;
	for (int32 Idx = 0; Idx < NumCells; ++Idx)
	{
		const FSeinFogOfWarCell& Cell = Asset->Cells[Idx];
		const FFixedPoint GroundZ = MinH + Q * FFixedPoint::FromInt(Cell.GroundHeight);
		const FFixedPoint BlockerRelZ = Q * FFixedPoint::FromInt(Cell.BlockerHeight);
		GroundHeight[Idx] = GroundZ;
		BlockerHeight[Idx] = (Cell.BlockerHeight > 0) ? (GroundZ + BlockerRelZ) : FFixedPoint::Zero;
		BlockerLayerMask[Idx] = Cell.BlockerLayerMask;
	}
}

// ============================================================================
// Init (no bake fallback)
// ============================================================================

void USeinFogOfWarDefault::InitGridFromVolumes(UWorld* World)
{
	if (!World) return;

	// Union all fog volume bounds (using editor-baked PlacedBounds — never
	// FromFloat at runtime), pick min cell size.
	FFixedVector UnionMin(FFixedPoint::FromInt(INT32_MAX), FFixedPoint::FromInt(INT32_MAX), FFixedPoint::FromInt(INT32_MAX));
	FFixedVector UnionMax(FFixedPoint::FromInt(INT32_MIN), FFixedPoint::FromInt(INT32_MIN), FFixedPoint::FromInt(INT32_MIN));
	FFixedPoint MinCellSize = FFixedPoint::Zero;
	int32 VolumeCount = 0;
	bool bAnyUnbaked = false;
	for (TActorIterator<ASeinFogOfWarVolume> It(World); It; ++It)
	{
		ASeinFogOfWarVolume* Vol = *It;
		if (!Vol) continue;

		// Snapshot guard: legacy actors fall back to runtime FromFloat
		// (NOT cross-arch deterministic). Re-saving the level after this
		// landed bakes the snapshot.
		FFixedVector VMin, VMax;
		if (Vol->bBoundsBaked)
		{
			VMin = Vol->PlacedBoundsMin;
			VMax = Vol->PlacedBoundsMax;
		}
		else
		{
			const FBox VB = Vol->GetVolumeWorldBounds();
			if (!VB.IsValid) continue;
			bAnyUnbaked = true;
			VMin = FFixedVector(FFixedPoint::FromFloat(VB.Min.X), FFixedPoint::FromFloat(VB.Min.Y), FFixedPoint::FromFloat(VB.Min.Z));
			VMax = FFixedVector(FFixedPoint::FromFloat(VB.Max.X), FFixedPoint::FromFloat(VB.Max.Y), FFixedPoint::FromFloat(VB.Max.Z));
		}

		if (VMin.X < UnionMin.X) UnionMin.X = VMin.X;
		if (VMin.Y < UnionMin.Y) UnionMin.Y = VMin.Y;
		if (VMin.Z < UnionMin.Z) UnionMin.Z = VMin.Z;
		if (VMax.X > UnionMax.X) UnionMax.X = VMax.X;
		if (VMax.Y > UnionMax.Y) UnionMax.Y = VMax.Y;
		if (VMax.Z > UnionMax.Z) UnionMax.Z = VMax.Z;

		const FFixedPoint VolCellSize = Vol->GetResolvedCellSize();
		if (VolumeCount == 0 || VolCellSize < MinCellSize)
		{
			MinCellSize = VolCellSize;
		}
		++VolumeCount;
	}
	if (VolumeCount == 0)
	{
		Width = 0;
		Height = 0;
		return;
	}
	if (bAnyUnbaked)
	{
		UE_LOG(LogSeinFogOfWarDefault, Warning,
			TEXT("InitGridFromVolumes: one or more fog volumes have stale "
				 "PlacedBounds (bBoundsBaked == false). Re-save the level to "
				 "bake snapshots. NOT cross-arch deterministic until then."));
	}

	CellSize = MinCellSize;
	Origin = UnionMin;
	const float CellSizeF = MinCellSize.ToFloat();
	const float SizeXF = (UnionMax.X - UnionMin.X).ToFloat();
	const float SizeYF = (UnionMax.Y - UnionMin.Y).ToFloat();
	Width = FMath::Max(1, FMath::CeilToInt(SizeXF / CellSizeF));
	Height = FMath::Max(1, FMath::CeilToInt(SizeYF / CellSizeF));

	const int32 NumCells = Width * Height;
	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumZeroed(NumCells);
	BlockerLayerMask.SetNumZeroed(NumCells);
	DynamicBlockerHeight.SetNumZeroed(NumCells);
	DynamicBlockerLayerMask.SetNumZeroed(NumCells);
	VisionGroups.Empty();
	SourceStates.Empty();
	LastDynamicBlockerHash = 0;

	// Per-cell downward trace capped at InitTraceCellCap — no-bake fallback.
	// Trace endpoints are runtime-only (the trace itself is non-deterministic
	// already — designers should bake to capture stable Z); just float them.
	const float OriginXF = UnionMin.X.ToFloat();
	const float OriginYF = UnionMin.Y.ToFloat();
	const float MinZF = UnionMin.Z.ToFloat();
	const float MaxZF = UnionMax.Z.ToFloat();

	const bool bTracePerCell = NumCells <= InitTraceCellCap;
	const FFixedPoint FallbackZ = FFixedPoint::FromFloat((MinZF + MaxZF) * 0.5f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinFowInitTrace), /*bTraceComplex*/ true);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float CX = OriginXF + (X + 0.5f) * CellSizeF;
			const float CY = OriginYF + (Y + 0.5f) * CellSizeF;
			FFixedPoint Z = FallbackZ;
			if (bTracePerCell)
			{
				FHitResult Hit;
				const FVector Start(CX, CY, MaxZF + 100.0f);
				const FVector End  (CX, CY, MinZF - 100.0f);
				if (World->LineTraceSingleByChannel(Hit, Start, End, BakeTraceChannel, Params))
				{
					Z = FFixedPoint::FromFloat(Hit.Location.Z);
				}
			}
			GroundHeight[CellIndex(X, Y)] = Z;
		}
	}

	UE_LOG(LogSeinFogOfWarDefault, Log,
		TEXT("InitGridFromVolumes: %d×%d cells @ %.0f cm (%d volumes, %d traces)"),
		Width, Height, CellSizeF, VolumeCount, bTracePerCell ? NumCells : 0);

	OnFogOfWarMutated.Broadcast();
}

// ============================================================================
// Tick / stamp
// ============================================================================

FSeinFogVisionGroup& USeinFogOfWarDefault::GetOrCreateGroup(FSeinPlayerID PlayerID)
{
	FSeinFogVisionGroup& Group = VisionGroups.FindOrAdd(PlayerID);
	const int32 NumCells = Width * Height;
	if (Group.CellBitfield.Num() != NumCells)
	{
		Group.CellBitfield.SetNumZeroed(NumCells);
	}
	return Group;
}

void USeinFogOfWarDefault::TickStamps(UWorld* World)
{
	if (Width <= 0 || Height <= 0 || !World) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinVisionData::StaticStruct());
	if (!Storage) return;

	// Rebuild dynamic blocker overlay first — smoke grenades / destructible
	// mid-animation / any runtime USeinExtentsComponent (bBlocksFogOfWar) entities stamp
	// their discs into DynamicBlockerHeight + DynamicBlockerLayerMask here,
	// so the vision passes below see the freshest occlusion state.
	const bool bDynamicBlockersChanged = RebuildDynamicBlockers(World);

	// If dynamic blockers changed, every source's LOS may have shifted —
	// invalidate every source state to force a re-stamp this tick.
	// Without this, a source whose XYZ + props are unchanged would keep
	// its stale footprint even though smoke just appeared/disappeared on
	// its sightline.
	if (bDynamicBlockersChanged)
	{
		// Tear down all footprints (decrement refcounts cleanly) — safer
		// than wiping bitfields directly because it keeps ref counters
		// consistent with whatever sources stay alive this tick.
		TArray<FSeinEntityHandle> ToInvalidate;
		ToInvalidate.Reserve(SourceStates.Num());
		for (const TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair : SourceStates)
		{
			if (Pair.Value.bValid) ToInvalidate.Add(Pair.Key);
		}
		for (FSeinEntityHandle H : ToInvalidate)
		{
			if (FSeinFogSourceState* S = SourceStates.Find(H))
			{
				if (FSeinFogVisionGroup* G = VisionGroups.Find(S->Owner))
				{
					DecrementFootprintsForState(*S, *G);
				}
				// Clear inputs so UpdateSourceStamp's change-detect
				// triggers a full re-stamp.
				S->bValid = false;
			}
		}
	}

	// Walk live entities. Each call is O(few-compares) on the stable path
	// — only sources whose inputs differ from last tick pay the full
	// remove-and-restamp cost.
	TSet<FSeinEntityHandle> AliveSources;
	AliveSources.Reserve(SourceStates.Num());

	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Storage, &AliveSources](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinVisionData* VData = static_cast<const FSeinVisionData*>(Raw);
			if (!VData) return;

			AliveSources.Add(Handle);
			const FSeinPlayerID OwnerPlayer = Sim->GetEntityOwner(Handle);
			UpdateSourceStamp(Handle, *VData,
				Entity.Transform.GetLocation(), Entity.Transform.Rotation, OwnerPlayer);
		});

	// Source went away (entity destroyed, vision component stripped, etc.) —
	// tear down its footprint so its bits don't linger.
	if (SourceStates.Num() != AliveSources.Num())
	{
		TArray<FSeinEntityHandle> Stale;
		for (const TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair : SourceStates)
		{
			if (!AliveSources.Contains(Pair.Key)) Stale.Add(Pair.Key);
		}
		for (FSeinEntityHandle H : Stale) RemoveSourceStamp(H);
	}

	OnFogOfWarMutated.Broadcast();
}

namespace
{
	/** Hash a vision source's stamp set. Folds shape geometry + layer mask
	 *  per stamp; XOR-combine across stamps so iteration order is irrelevant
	 *  (matches the existing dynamic-blocker fingerprint pattern). Used by
	 *  UpdateSourceStamp's stable-fast-path compare. */
	uint32 HashVisionStamps(const TArray<FSeinVisionStamp>& Stamps)
	{
		uint32 H = 0;
		for (const FSeinVisionStamp& S : Stamps)
		{
			H ^= GetTypeHash(S.Shape);
			H ^= static_cast<uint32>(S.LayerMask);
		}
		return H;
	}
}

void USeinFogOfWarDefault::UpdateSourceStamp(FSeinEntityHandle Handle,
	const FSeinVisionData& VData, const FFixedVector& WorldPos,
	const FFixedQuaternion& Rotation, FSeinPlayerID Owner)
{
	FSeinFogSourceState& State = SourceStates.FindOrAdd(Handle);

	const uint32 NewStampsHash = HashVisionStamps(VData.Stamps);

	// Stable-source fast path. Identical pose + stamp set ⇒ no work. Pose
	// includes Rotation now since shaped stamps depend on it (rect axes,
	// cone direction). Stamps hash folds shape geometry + per-stamp layer
	// mask + bEnabled flags, so toggling a garrison-cone on or off
	// invalidates the cache as expected.
	if (State.bValid
		&& State.Owner == Owner
		&& State.WorldPos == WorldPos
		&& State.Rotation == Rotation
		&& State.EyeHeight == VData.EyeHeight
		&& State.StampsHash == NewStampsHash)
	{
		return;
	}

	// Tear down old footprints on whichever group OWNED them last tick — owner
	// can change (entity transferred between players), so we decrement on the
	// OLD group, then stamp into the NEW one below.
	if (State.bValid)
	{
		if (FSeinFogVisionGroup* OldGroup = VisionGroups.Find(State.Owner))
		{
			DecrementFootprintsForState(State, *OldGroup);
		}
	}

	FSeinFogVisionGroup& NewGroup = GetOrCreateGroup(Owner);

	// One pass per stamp × per layer-bit set in its mask. A stamp with mask
	// 0x06 (V | N0) makes two StampLayerFootprint calls — one to bit 1, one
	// to bit 2 — and accumulates into both Footprints[1] and Footprints[2]
	// for tear-down on next change.
	for (const FSeinVisionStamp& VStamp : VData.Stamps)
	{
		if (!VStamp.Shape.bEnabled) continue;
		if (VStamp.LayerMask == 0) continue;

		// Iterate set bits. We only emit on bits 1..7 — bit 0 (Explored) is
		// auto-stamped on every cell visit by StampLayerFootprint and never
		// participates in the LayerMask itself.
		for (uint8 Bit = 1; Bit <= 7; ++Bit)
		{
			const uint8 BitMask = static_cast<uint8>(1u << Bit);
			if ((VStamp.LayerMask & BitMask) == 0) continue;
			StampLayerFootprint(NewGroup, VStamp.Shape, WorldPos, Rotation,
				VData.EyeHeight, Bit, State.Footprints[Bit]);
		}
	}

	// Commit cache key for next tick's compare.
	State.bValid = true;
	State.Owner = Owner;
	State.WorldPos = WorldPos;
	State.Rotation = Rotation;
	State.EyeHeight = VData.EyeHeight;
	State.StampsHash = NewStampsHash;
}

void USeinFogOfWarDefault::RemoveSourceStamp(FSeinEntityHandle Handle)
{
	FSeinFogSourceState* State = SourceStates.Find(Handle);
	if (!State) return;
	if (State->bValid)
	{
		if (FSeinFogVisionGroup* Group = VisionGroups.Find(State->Owner))
		{
			DecrementFootprintsForState(*State, *Group);
		}
	}
	SourceStates.Remove(Handle);
}

void USeinFogOfWarDefault::StampLayerFootprint(FSeinFogVisionGroup& Group,
	const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos,
	const FFixedQuaternion& EntityRotation,
	FFixedPoint EyeHeight, uint8 StampBit,
	TArray<int32>& OutFootprint)
{
	if (StampBit < 1 || StampBit > 7) return;
	if (CellSize <= FFixedPoint::Zero) return;
	if (!Shape.bEnabled) return;

	// Apex world position = entity pos + Quat(EntityYaw)·LocalOffset. For
	// radial stamps this collapses to the entity itself; for window cones
	// or rect stamps it's wherever the designer placed the LocalOffset.
	// LOS originates from this apex (so a window cone casts FROM the
	// window, not the building center).
	const FFixedVector ApexWorld = SeinStampUtils::ComputeStampWorldOrigin(
		Shape, EntityWorldPos, EntityRotation);
	int32 SX, SY;
	if (!WorldToGrid(ApexWorld, SX, SY)) return;

	const int32 NumCells = Width * Height;

	// Lazy-allocate the per-bit refcount array on first stamp into this
	// bit. Bit 0 (Explored) is sticky and never refcounted.
	TArray<uint16>& RefCounts = Group.RefCounts[StampBit];
	if (RefCounts.Num() != NumCells)
	{
		RefCounts.SetNumZeroed(NumCells);
	}

	// Bitfield exists too (lazy on first stamp into a new group).
	if (Group.CellBitfield.Num() != NumCells)
	{
		Group.CellBitfield.SetNumZeroed(NumCells);
	}

	// Eye Z = entity's actual sim Z + EyeHeight. Uses `EntityWorldPos.Z`
	// rather than GroundHeight[cell] so a unit standing on a climbable
	// platform sees from its actual standing surface, not the terrain
	// beneath. Deterministic (FFixedPoint).
	const FFixedPoint EyeZ = EntityWorldPos.Z + EyeHeight;
	const uint8 StampBitMask = static_cast<uint8>(1u << StampBit);

	// Helper — increment refcount, set bit on 0→1, always set Explored
	// (sticky, no refcount). Records the cell into OutFootprint for
	// later tear-down. Multiple stamps on the same source emitting the
	// same bit safely accumulate (refcount handles duplicate increments).
	auto Stamp = [&](int32 CellIdx)
	{
		OutFootprint.Add(CellIdx);
		uint16& Count = RefCounts[CellIdx];
		if (Count == 0)
		{
			Group.CellBitfield[CellIdx] |= StampBitMask;
		}
		++Count;
		Group.CellBitfield[CellIdx] |= SEIN_FOW_BIT_EXPLORED;
	};

	// Apex cell — always visible to its own source on this layer (no LOS
	// check; the apex literally IS the eye).
	Stamp(CellIndex(SX, SY));

	// Walk every cell inside the shape's coverage. For each one, run LOS
	// from the apex cell to the target cell — the apex eye Z plus the
	// target's terrain Z drive the lampshade interpolation in
	// HasLineOfSightToCell. Same Bresenham walk as before; the only thing
	// the shape primitive changes is which cells are CANDIDATES.
	SeinStampUtils::ForEachCoveredCell(
		Shape, EntityWorldPos, EntityRotation,
		CellSize, Origin, Width, Height,
		[&](int32 TX, int32 TY)
		{
			// Apex cell already stamped above — skip duplicate work.
			if (TX == SX && TY == SY) return;

			const int32 TargetIdx = CellIndex(TX, TY);
			const FFixedPoint TargetZ = GroundHeight.IsValidIndex(TargetIdx)
				? GroundHeight[TargetIdx] : Origin.Z;

			if (HasLineOfSightToCell(SX, SY, TX, TY, EyeZ, TargetZ, StampBitMask))
			{
				Stamp(TargetIdx);
			}
		});
}

void USeinFogOfWarDefault::DecrementFootprintsForState(FSeinFogSourceState& State, FSeinFogVisionGroup& Group)
{
	for (int32 BitIdx = 1; BitIdx <= 7; ++BitIdx)
	{
		TArray<int32>& Footprint = State.Footprints[BitIdx];
		if (Footprint.Num() == 0) continue;
		TArray<uint16>& RefCounts = Group.RefCounts[BitIdx];
		if (RefCounts.Num() == 0)
		{
			// Refcount array missing — defensive; just clear the
			// footprint so we don't leak it into the next stamp.
			Footprint.Reset();
			continue;
		}
		const uint8 BitMask = static_cast<uint8>(1u << BitIdx);
		for (int32 CellIdx : Footprint)
		{
			if (!RefCounts.IsValidIndex(CellIdx)) continue;
			uint16& Count = RefCounts[CellIdx];
			if (Count > 0)
			{
				--Count;
				if (Count == 0 && Group.CellBitfield.IsValidIndex(CellIdx))
				{
					Group.CellBitfield[CellIdx] &= ~BitMask;
				}
			}
		}
		Footprint.Reset();
	}
}

bool USeinFogOfWarDefault::HasLineOfSightToCell(int32 X0, int32 Y0, int32 X1, int32 Y1,
	FFixedPoint EyeZ, FFixedPoint TargetZ, uint8 StampBitMask) const
{
	if (X0 == X1 && Y0 == Y1) return true;

	// Classic Bresenham — integer-native, symmetric, same walk from either
	// endpoint. Source cell is the starting point (not tested); target
	// cell is whatever we're trying to see (not tested — you can always
	// "see" the thing you're looking at, even if it's a wall). Opacity
	// test applies to every cell in between, filtered by the stamp's
	// layer + checked against the ray's interpolated Z at that cell.
	const int32 DXabs = FMath::Abs(X1 - X0);
	const int32 DYabs = FMath::Abs(Y1 - Y0);
	const int32 TotalSteps = FMath::Max(DXabs, DYabs); // ≥ 1 by the endpoint check above

	// Deterministic lampshade ray. Precompute one StepZ outside the loop
	// and accumulate per iteration: avoids an FFixedPoint mul + div on
	// the inner-loop hot path (replaces them with a single add). Result
	// at step i is EyeZ + StepZ·i, same as EyeZ + ΔZ·i/N to within FFixedPoint
	// quantum. Deterministic across clients because every client does the
	// same sequence of integer adds in the same order.
	const FFixedPoint StepZ = (TargetZ - EyeZ) / FFixedPoint::FromInt(TotalSteps);
	FFixedPoint RayZ = EyeZ;

	const int32 DX =  DXabs;
	const int32 DY = -DYabs;
	const int32 SXStep = (X0 < X1) ? 1 : -1;
	const int32 SYStep = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX + DY;
	int32 X = X0;
	int32 Y = Y0;

	while (true)
	{
		const int32 E2 = 2 * Err;
		if (E2 >= DY) { Err += DY; X += SXStep; }
		if (E2 <= DX) { Err += DX; Y += SYStep; }
		RayZ += StepZ;

		if (X == X1 && Y == Y1) return true;
		if (IsCellOpaqueToEye(X, Y, RayZ, StampBitMask)) return false;
	}
}

bool USeinFogOfWarDefault::IsCellOpaqueToEye(int32 X, int32 Y, FFixedPoint EyeZ, uint8 StampBitMask) const
{
	if (!IsValidCoord(X, Y)) return false;
	const int32 Idx = CellIndex(X, Y);
	const FFixedPoint GroundZ  = GroundHeight.IsValidIndex(Idx)  ? GroundHeight[Idx]  : FFixedPoint::Zero;

	// Terrain (ground) always occludes — hills block line of sight
	// regardless of which layer you're stamping. Height is height.
	if (GroundZ > EyeZ) return true;

	// Static blocker (from bake) — only occludes this layer if the
	// blocker's LayerMask covers the stamping bit.
	const FFixedPoint StaticZ = BlockerHeight.IsValidIndex(Idx) ? BlockerHeight[Idx] : FFixedPoint::Zero;
	if (StaticZ > EyeZ)
	{
		const uint8 Mask = BlockerLayerMask.IsValidIndex(Idx) ? BlockerLayerMask[Idx] : 0;
		if ((Mask & StampBitMask) != 0) return true;
	}

	// Dynamic blocker (runtime — smoke grenades, destructibles mid-anim)
	// tests independently. A short-but-layered dynamic blocker doesn't
	// inherit a tall static's reach, and vice versa.
	const FFixedPoint DynZ = DynamicBlockerHeight.IsValidIndex(Idx) ? DynamicBlockerHeight[Idx] : FFixedPoint::Zero;
	if (DynZ > EyeZ)
	{
		const uint8 Mask = DynamicBlockerLayerMask.IsValidIndex(Idx) ? DynamicBlockerLayerMask[Idx] : 0;
		if ((Mask & StampBitMask) != 0) return true;
	}

	return false;
}

bool USeinFogOfWarDefault::RebuildDynamicBlockers(UWorld* World)
{
	// Clear last tick's overlay. FMemory::Memzero is fine on FFixedPoint's
	// int64 backing — value 0 is the struct's "no blocker" sentinel.
	if (DynamicBlockerHeight.Num() > 0)
	{
		FMemory::Memzero(DynamicBlockerHeight.GetData(),
			DynamicBlockerHeight.Num() * sizeof(FFixedPoint));
	}
	if (DynamicBlockerLayerMask.Num() > 0)
	{
		FMemory::Memzero(DynamicBlockerLayerMask.GetData(),
			DynamicBlockerLayerMask.Num() * sizeof(uint8));
	}

	// Whether to force every source's stable-fast-path cache to invalidate
	// this tick. Computed at end via hash compare.
	auto FinalizeReturn = [&](uint32 NewHash) -> bool
	{
		const bool bChanged = (NewHash != LastDynamicBlockerHash);
		LastDynamicBlockerHash = NewHash;
		return bChanged;
	};

	if (!World) return FinalizeReturn(0);
	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return FinalizeReturn(0);
	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinExtentsData::StaticStruct());
	if (!Storage)
	{
		// Verbose so users can spot this without VeryVerbose. The most
		// common cause of "smoke isn't blocking": no entity in the world
		// has FSeinExtentsData in component storage (level-placed actor not
		// registered, or component not on the BP).
		UE_LOG(LogSeinFogOfWarDefault, Verbose,
			TEXT("RebuildDynamicBlockers: no FSeinExtentsData storage registered "
				 "— no entity with USeinExtentsComponent has been spawned via SpawnEntity"));
		return FinalizeReturn(0);
	}

	// Walk every alive entity; stamp every shape on every extents that has
	// bBlocksFogOfWar set. Per-shape Height drives occluder height (max-per-
	// cell across overlapping shapes). Smoke-grenade pattern: spawn an
	// entity with FSeinExtentsData via an ability, set bBlocksFogOfWar=true
	// + a Capsule shape with the smoke's radius/height; the subsystem picks
	// it up automatically.
	int32 NumStamps = 0;
	uint32 NewHash = 0;
	Sim->GetEntityPool().ForEachEntity(
		[this, Storage, &NumStamps, &NewHash](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinExtentsData* Extents = static_cast<const FSeinExtentsData*>(Raw);
			if (!Extents) return;
			if (!Extents->bBlocksFogOfWar) return;
			if (Extents->BlockedFogLayerMask == 0) return;
			if (Extents->Shapes.Num() == 0) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();
			const FFixedQuaternion Rot = Entity.Transform.Rotation;

			// Fold entity-level fields into the change-detection fingerprint
			// once. XOR is order-independent so swapping iteration order
			// produces the same fingerprint — only adds / removes / edits
			// flip the hash.
			NewHash ^= GetTypeHash(Handle.Index);
			NewHash ^= GetTypeHash(Pos.X.Value);
			NewHash ^= GetTypeHash(Pos.Y.Value);
			NewHash ^= GetTypeHash(Pos.Z.Value);
			NewHash ^= static_cast<uint32>(Extents->BlockedFogLayerMask);

			for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
			{
				if (ExtShape.Height <= FFixedPoint::Zero) continue;

				const FSeinStampShape PlanarStamp = ExtShape.AsStampShape();
				StampDynamicBlockerShape(PlanarStamp, Pos, Rot,
					ExtShape.Height, Extents->BlockedFogLayerMask);
				++NumStamps;
				NewHash ^= GetTypeHash(ExtShape);
			}
		});
	// Verbose every tick — single line confirms the function is firing
	// AND tells you how many stamps it picked up. Diff against the number
	// of enabled stamps across all live FSeinExtentsComponent (bBlocksFogOfWar)
	// entities (one entity with 4 enabled stamps = 4 here).
	UE_LOG(LogSeinFogOfWarDefault, Verbose,
		TEXT("RebuildDynamicBlockers: stamped %d shape(s) across blocker entities"),
		NumStamps);

	return FinalizeReturn(NewHash);
}

void USeinFogOfWarDefault::StampDynamicBlockerShape(const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos, const FFixedQuaternion& EntityRotation,
	FFixedPoint HeightAboveGround, uint8 LayerMask)
{
	if (HeightAboveGround <= FFixedPoint::Zero) return;
	if (LayerMask == 0) return;

	// Lazily resize the dynamic-blocker overlay to grid extent if the
	// caller forgot to (defensive — RebuildDynamicBlockers handles the
	// clear, but a fresh load with no clear yet would otherwise crash on
	// the IsValidIndex below).
	const int32 NumCells = Width * Height;
	if (DynamicBlockerHeight.Num() != NumCells) DynamicBlockerHeight.SetNumZeroed(NumCells);
	if (DynamicBlockerLayerMask.Num() != NumCells) DynamicBlockerLayerMask.SetNumZeroed(NumCells);

	// Walk the shape's covered cells; OR the layer mask + take the taller
	// of (existing height, ground + this stamp's height) per cell. Multiple
	// overlapping stamps on the same blocker (or multiple blockers stamping
	// the same cell) correctly produce the union of layers + the highest
	// occluder height.
	SeinStampUtils::ForEachCoveredCell(
		Shape, EntityWorldPos, EntityRotation,
		CellSize, Origin, Width, Height,
		[&](int32 X, int32 Y)
		{
			const int32 Idx = CellIndex(X, Y);
			const FFixedPoint GroundZ = GroundHeight.IsValidIndex(Idx)
				? GroundHeight[Idx] : FFixedPoint::Zero;
			const FFixedPoint TopZ = GroundZ + HeightAboveGround;
			if (TopZ > DynamicBlockerHeight[Idx]) DynamicBlockerHeight[Idx] = TopZ;
			DynamicBlockerLayerMask[Idx] |= LayerMask;
		});
}

uint8 USeinFogOfWarDefault::GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const
{
	int32 CX, CY;
	if (!WorldToGrid(WorldPos, CX, CY)) return 0;
	const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
	if (!Group) return 0;
	const int32 Idx = CellIndex(CX, CY);
	return Group->CellBitfield.IsValidIndex(Idx) ? Group->CellBitfield[Idx] : 0;
}

uint8 USeinFogOfWarDefault::GetEntityVisibleBits(FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const
{
	const FSeinEntity* Entity = Sim.GetEntity(Target);
	if (!Entity) return 0;

	const FFixedVector EntityPos = Entity->Transform.GetLocation();
	const FFixedQuaternion EntityRot = Entity->Transform.Rotation;

	// Single-point query at center, used both as the fallback for
	// extents-less entities and as the seed for volumetric OR (covers the
	// entity's exact center cell even if no stamp's footprint hits it
	// exactly — degenerate cases like zero-sized stamps).
	uint8 Bits = GetCellBitfield(Observer, EntityPos);

	const FSeinExtentsData* Extents = Sim.GetComponent<FSeinExtentsData>(Target);
	if (!Extents || Extents->Shapes.Num() == 0)
	{
		return Bits;
	}

	const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
	if (!Group || Group->CellBitfield.Num() == 0) return Bits;

	// OR every cell each shape covers. AsStampShape() converts the volumetric
	// primitive to its planar equivalent (Box→Rect, Capsule→Radial) so we
	// reuse SeinStampUtils for the actual cell rasterization. Once Bits hits
	// every layer (0xFF), early-out — further iteration can't add anything.
	for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
	{
		if (Bits == 0xFF) break;

		const FSeinStampShape PlanarStamp = ExtShape.AsStampShape();
		SeinStampUtils::ForEachCoveredCell(
			PlanarStamp, EntityPos, EntityRot,
			CellSize, Origin, Width, Height,
			[&](int32 X, int32 Y)
			{
				const int32 Idx = CellIndex(X, Y);
				if (Group->CellBitfield.IsValidIndex(Idx))
				{
					Bits |= Group->CellBitfield[Idx];
				}
			});
	}

	return Bits;
}

void USeinFogOfWarDefault::CollectDebugCellQuads(FSeinPlayerID Observer,
	int32 VisibleBitIndex,
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
	if (Width <= 0 || Height <= 0) return;

	// 0.9 inset → ~10% gap between neighboring quads. Reads as a grid (matches
	// USeinNavigationDefaultAStar::CollectDebugCellQuads). Also dodges any z-fight at
	// exact cell boundaries between same-Z cells.
	OutHalfExtent = CellSize.ToFloat() * 0.5f * 0.9f;
	const int32 NumCells = Width * Height;
	OutCenters.Reserve(NumCells);
	OutColors.Reserve(NumCells);

	const float OriginXF = Origin.X.ToFloat();
	const float OriginYF = Origin.Y.ToFloat();
	const float CellSizeF = CellSize.ToFloat();
	const float HalfCellF = CellSizeF * 0.5f;

	// Per-observer bitfield lookup. If the observer has never stamped,
	// there's no group — every cell renders as baseline (non-PIE / editor
	// preview / unknown observer).
	const FSeinFogVisionGroup* ObserverGroup = VisionGroups.Find(Observer);
	const TArray<uint8>* ObserverBits = ObserverGroup ? &ObserverGroup->CellBitfield : nullptr;

	// Clamp bit index to valid EVNNNNNN range; anything out-of-range falls
	// back to the V bit (1).
	if (VisibleBitIndex < 0 || VisibleBitIndex > 7) VisibleBitIndex = 1;
	const uint8 VisibleMask = static_cast<uint8>(1u << VisibleBitIndex);

	// Paint colors. "Visible" color comes from the module helper (yellow for
	// E, cyan for V, plugin-settings DebugColor for N0..N5). Blocker red and
	// baseline black are framework-fixed — keep them readable across any
	// layer perspective.
	const FColor VisibleColor = UE::SeinARTSFogOfWar::GetDebugLayerColor(VisibleBitIndex);
	const FColor Red  (200, 0, 0);
	const FColor Black(0, 0, 0);

	int32 NumDynamicBlockerCells = 0;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 Bits = (ObserverBits && ObserverBits->IsValidIndex(Idx)) ? (*ObserverBits)[Idx] : 0;
			const bool bVisible = (Bits & VisibleMask) != 0;

			// Static + dynamic blocker masks per the viewed layer. Treated
			// asymmetrically below: dynamic blockers always show red (smoke
			// grenades / destructibles need unambiguous viz so designers
			// can see the stamp footprint regardless of LOS state), while
			// static blockers keep the legacy "visible wins" behavior so a
			// walkable rooftop renders blue when a unit stands on it.
			const uint8 StaticMask = BlockerLayerMask.IsValidIndex(Idx) ? BlockerLayerMask[Idx] : 0;
			const uint8 DynMask    = DynamicBlockerLayerMask.IsValidIndex(Idx) ? DynamicBlockerLayerMask[Idx] : 0;
			const bool bHasDynamicBlocker = (DynMask    & VisibleMask) != 0;
			const bool bHasStaticBlocker  = (StaticMask & VisibleMask) != 0;

			const FFixedPoint GroundZ  = GroundHeight.IsValidIndex(Idx) ? GroundHeight[Idx] : Origin.Z;
			const FFixedPoint StaticZ  = (StaticMask != 0 && BlockerHeight.IsValidIndex(Idx)) ? BlockerHeight[Idx] : GroundZ;
			const FFixedPoint DynZ     = (DynMask != 0 && DynamicBlockerHeight.IsValidIndex(Idx)) ? DynamicBlockerHeight[Idx] : GroundZ;
			// Render at the tallest occluder so smoke + wall at the same
			// cell draws at the smoke top if smoke is taller (which is
			// visually correct — it's what the unit sees).
			const FFixedPoint BlockZ   = (DynZ > StaticZ) ? DynZ : StaticZ;
			const FFixedPoint TopZ     = (BlockZ > GroundZ) ? BlockZ : GroundZ;

			// Priority:
			//   1. Dynamic blocker (smoke grenades, destructibles) — always
			//      red. LOS rays reaching a smoke cell as their target would
			//      otherwise mark it "visible" and hide the stamp; designers
			//      need the footprint visible at all times.
			//   2. Visible to observer — cyan/layer-color. A unit standing
			//      on a roof cell still reads as visible here (eye Z over
			//      blocker top is the elevated-LOS case).
			//   3. Static blocker — red, only when not visible (legacy
			//      behavior, preserves rooftop-walkable viz).
			//   4. Baseline black.
			FColor Color;
			FFixedPoint WZFP;
			if (bHasDynamicBlocker)
			{
				Color = Red;
				// Render at ground (not DynZ = ground + HeightAboveGround).
				// Tall smoke (~400cm) at DynZ would float a quad 400cm above
				// the surrounding grid; flush-with-grid reads cleaner and the
				// blocker height still drives shadowcast correctly.
				WZFP = GroundZ;
				++NumDynamicBlockerCells;
			}
			else if (bVisible)
			{
				Color = VisibleColor;
				WZFP = TopZ;
			}
			else if (bHasStaticBlocker)
			{
				Color = Red;
				WZFP = StaticZ;
			}
			else
			{
				Color = Black;
				WZFP = GroundZ;
			}

			const float WX = OriginXF + CellSizeF * X + HalfCellF;
			const float WY = OriginYF + CellSizeF * Y + HalfCellF;

			OutCenters.Emplace(WX, WY, WZFP.ToFloat() + SEIN_FOW_DEBUG_Z_OFFSET);
			OutColors.Add(Color);
		}
	}

	UE_LOG(LogSeinFogOfWarDefault, Verbose,
		TEXT("CollectDebugCellQuads (ViewBit=%d): %d total cells, %d dynamic-blocker cells (red)"),
		VisibleBitIndex, Width * Height, NumDynamicBlockerCells);
}

bool USeinFogOfWarDefault::WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const
{
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
	const FFixedPoint LocalX = WorldPos.X - Origin.X;
	const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
	const FFixedPoint FX = LocalX / CellSize;
	const FFixedPoint FY = LocalY / CellSize;
	OutX = FX.ToInt();
	OutY = FY.ToInt();
	return IsValidCoord(OutX, OutY);
}
