/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStar.cpp
 */

#include "Default/SeinNavigationAStar.h"
#include "Default/SeinNavigationAStarAsset.h"
#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinNavLayerDefinition.h"
#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Components/ActorComponents/SeinMovementComponent.h"
#include "Stamping/SeinStampUtils.h"
#include "SeinARTSNavigationModule.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Components/BrushComponent.h"
#include "DrawDebugHelpers.h"
#include "Math/Box.h"
#include "Misc/ScopedSlowTask.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSeinNavAStar, Log, All);

// ============================================================================
// Asset class
// ============================================================================

TSubclassOf<USeinNavigationAsset> USeinNavigationAStar::GetAssetClass() const
{
	return USeinNavigationAStarAsset::StaticClass();
}

// ============================================================================
// Bake
// ============================================================================

bool USeinNavigationAStar::BeginBake(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogSeinNavAStar, Warning, TEXT("BeginBake: null world"));
		return false;
	}
	if (bBaking)
	{
		UE_LOG(LogSeinNavAStar, Warning, TEXT("BeginBake: already baking"));
		return false;
	}

	bBaking = true;
	bCancelRequested = false;
	ON_SCOPE_EXIT { bBaking = false; bCancelRequested = false; };

	USeinNavigationAStarAsset* NewAsset = nullptr;
	const bool bOk = DoSyncBake(World, NewAsset);
	if (!bOk || !NewAsset)
	{
		UE_LOG(LogSeinNavAStar, Warning, TEXT("BeginBake: failed"));
		return false;
	}

	// Point every NavVolume at the new asset + point this nav at its data.
	for (TActorIterator<ASeinNavVolume> It(World); It; ++It)
	{
		It->BakedAsset = NewAsset;
		It->MarkPackageDirty();
	}
	LoadFromAsset(NewAsset);

	UE_LOG(LogSeinNavAStar, Log, TEXT("Bake complete: %dx%d cells, CellSize=%s"),
		NewAsset->Width, NewAsset->Height, *NewAsset->CellSize.ToString());
	return true;
}

bool USeinNavigationAStar::DoSyncBake(UWorld* World, USeinNavigationAStarAsset*& OutAsset)
{
	OutAsset = nullptr;

	// Gather volumes + union their bounds.
	TArray<ASeinNavVolume*> Volumes;
	FBox UnionBounds(ForceInit);
	for (TActorIterator<ASeinNavVolume> It(World); It; ++It)
	{
		ASeinNavVolume* Vol = *It;
		if (!Vol) continue;
		Volumes.Add(Vol);
		UnionBounds += Vol->GetVolumeWorldBounds();
	}
	if (Volumes.Num() == 0 || !UnionBounds.IsValid)
	{
		UE_LOG(LogSeinNavAStar, Warning, TEXT("DoSyncBake: no NavVolumes in world"));
		return false;
	}

	// Resolve bake parameters from the first volume (per-volume overrides
	// shadow plugin-settings defaults; later volumes use their own resolved
	// values for their region — MVP uses a single global CellSize).
	// Getter returns FFixedPoint directly — the only float touched here is
	// the bake-time trace-math float, computed once via ToFloat().
	const FFixedPoint BakedCellSize = Volumes[0]->GetResolvedCellSize();
	const float CellSizeF = BakedCellSize.ToFloat();

	const int32 GridW = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.X - UnionBounds.Min.X) / CellSizeF));
	const int32 GridH = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.Y - UnionBounds.Min.Y) / CellSizeF));
	const FVector OriginWorld(UnionBounds.Min.X, UnionBounds.Min.Y, UnionBounds.Min.Z);
	const float TopZ = UnionBounds.Max.Z + BakeTraceHeadroom;
	const float BottomZ = UnionBounds.Min.Z - 10.0f;

#if WITH_EDITOR
	FScopedSlowTask Task(GridW * GridH, NSLOCTEXT("SeinNavAStar", "Baking", "Baking SeinARTS A* Nav..."));
	Task.MakeDialog(true /*bShowCancelButton*/);
#endif

	// Create asset (editor) or transient package (runtime bake).
#if WITH_EDITOR
	const FString AssetName = FString::Printf(TEXT("NavData_%s"), *World->GetMapName());
	OutAsset = CreateOrLoadAsset(World, AssetName);
#else
	OutAsset = NewObject<USeinNavigationAStarAsset>(GetTransientPackage());
#endif
	if (!OutAsset) return false;

	OutAsset->Width = GridW;
	OutAsset->Height = GridH;
	OutAsset->CellSize = BakedCellSize;
	OutAsset->Origin = FFixedVector(FFixedPoint::FromFloat(OriginWorld.X),
	                                FFixedPoint::FromFloat(OriginWorld.Y),
	                                FFixedPoint::FromFloat(OriginWorld.Z));
	OutAsset->Cells.SetNum(GridW * GridH);

	const float MaxSlopeCos = FMath::Cos(FMath::DegreesToRadians(MaxWalkableSlopeDegrees));

	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinNavBake), true /*bTraceComplex*/);
	for (ASeinNavVolume* Vol : Volumes)
	{
		if (Vol) QP.AddIgnoredActor(Vol);
	}

	// Per-archetype skip list. Two reasons to exclude an ASeinActor's geometry
	// from the static bake:
	//   1) Designer flagged the archetype as `bBakesIntoNav = false` (e.g. a
	//      decorative prop, a lampshade, an entity that should be walkable
	//      through). Hard override.
	//   2) Mobile heuristic — the archetype CDO carries a USeinMovementComponent.
	//      Vehicles, infantry, and any other unit that runs locomotion should
	//      not carve their pose-at-bake-time into the static nav. Designers
	//      who DO want a stationary unit baked can flip `bBakesIntoNav` off
	//      on a non-mobile archetype, or simply not give it a movement
	//      component.
	// `QP.AddIgnoredActor` ignores every primitive on the actor (skeletal
	// meshes, collision capsules, brush components — the lot), so the bake
	// trace passes through cleanly regardless of how the BP is composed.
	int32 NumIgnoredActors = 0;
	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* SeinActor = *It;
		if (!SeinActor) continue;

		bool bSkip = false;

		// Hard-override flag.
		if (const USeinArchetypeDefinition* ArchDef = SeinActor->ArchetypeDefinition)
		{
			if (!ArchDef->bBakesIntoNav)
			{
				bSkip = true;
			}
		}

		// Mobile heuristic — any USeinMovementComponent present means the
		// archetype is locomotion-driven, so its at-bake pose is irrelevant.
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
	UE_LOG(LogSeinNavAStar, Log,
		TEXT("Bake: ignoring %d SeinActor(s) (mobile + bBakesIntoNav=false)"),
		NumIgnoredActors);

	// Diagnostic counters. Log at end-of-bake. Turn to Verbose to silence later.
	int32 BakeMissed = 0, BakeWalkable = 0, BakeBlocked = 0;

	int32 Processed = 0;
	for (int32 Y = 0; Y < GridH; ++Y)
	{
		for (int32 X = 0; X < GridW; ++X)
		{
			if (bCancelRequested)
			{
				UE_LOG(LogSeinNavAStar, Warning, TEXT("Bake cancelled by user"));
				OutAsset = nullptr;
				return false;
			}

			const float CenterX = OriginWorld.X + (X + 0.5f) * CellSizeF;
			const float CenterY = OriginWorld.Y + (Y + 0.5f) * CellSizeF;
			const FVector Start(CenterX, CenterY, TopZ);
			const FVector End(CenterX, CenterY, BottomZ);

			FSeinAStarCell& Cell = OutAsset->Cells[Y * GridW + X];

			// Trace down from above; record whatever walkable-topped surface we
			// hit. A cube top and the floor next to it both register as walkable
			// cells — what prevents units from climbing the cube is the slope
			// gate in A* neighbor transitions (|ΔZ| / cellDist > tan(max slope)
			// → that edge is not traversable). Same behavior as UE NavMesh.
			FHitResult TopHit;
			if (!World->LineTraceSingleByChannel(TopHit, Start, End, ECC_Visibility, QP))
			{
				Cell.Cost = 0;
				Cell.Height = FFixedPoint::FromFloat(BottomZ);
				++BakeMissed;
				++BakeBlocked;
			}
			else
			{
				const float Dot = FVector::DotProduct(TopHit.Normal, FVector::UpVector);
				const bool bSlopeOK = Dot >= MaxSlopeCos;
				Cell.Cost = bSlopeOK ? 1 : 0;
				Cell.Height = FFixedPoint::FromFloat(TopHit.ImpactPoint.Z);
				if (Cell.Cost == 0) ++BakeBlocked; else ++BakeWalkable;
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

	// ========================================================================
	// Connectivity pass — detect physical gaps / obstacles between adjacent
	// walkable cells. For each cell A and each of its 8 neighbor directions,
	// sample the surface at the midpoint between cell centers. The edge is
	// traversable iff both half-steps (A→mid, mid→B) pass the slope gate AND
	// no half-step exceeds MaxStepHeight. Catches the "ramp-top → gap → cube-
	// top" jump where A.Z ≈ B.Z but the geometry between them drops off.
	// ========================================================================
	{
		// Per-cell max step height: first containing volume's override wins,
		// else plugin-settings fallback. Testing each cell against the volume
		// AABB list is O(cells × volumes) — fine for typical bakes (few dozen
		// volumes, tens of thousands of cells). If that ever shifts, swap to a
		// spatial hash of volume bounds.
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const FFixedPoint FallbackStepFP = Settings ? Settings->DefaultMaxStepHeight : FFixedPoint::FromInt(50);

		TArray<FFixedPoint> CellMaxStep;
		CellMaxStep.SetNum(GridW * GridH);
		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				const float CX = OriginWorld.X + (X + 0.5f) * CellSizeF;
				const float CY = OriginWorld.Y + (Y + 0.5f) * CellSizeF;
				FFixedPoint CellStep = FallbackStepFP;
				for (ASeinNavVolume* Vol : Volumes)
				{
					if (!Vol) continue;
					const FBox VB = Vol->GetVolumeWorldBounds();
					// XY containment only — bake spans the full vertical extent
					// of the union, and cells inside a volume in plan view take
					// its step rule regardless of Z.
					if (CX >= VB.Min.X && CX <= VB.Max.X && CY >= VB.Min.Y && CY <= VB.Max.Y)
					{
						CellStep = Vol->GetResolvedMaxStepHeight();
						break; // first match wins
					}
				}
				CellMaxStep[Y * GridW + X] = CellStep;
			}
		}

		const float BakeTan = FMath::Tan(FMath::DegreesToRadians(MaxWalkableSlopeDegrees));
		const FFixedPoint BakeMaxSlopeTanSq = FFixedPoint::FromFloat(BakeTan * BakeTan);
		const FFixedPoint CellSizeFP = BakedCellSize;
		const FFixedPoint HalfCellSq = (CellSizeFP * CellSizeFP) / FFixedPoint::FromInt(4);   // (cell/2)² cardinal half-step
		const FFixedPoint HalfDiagSq = HalfCellSq * FFixedPoint::FromInt(2);                  // (cell/2 × √2)² diagonal half-step

		static const int32 DX8[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
		static const int32 DY8[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };

		int32 BakeEdges = 0, BakeBlockedEdges = 0;

		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				FSeinAStarCell& A = OutAsset->Cells[Y * GridW + X];
				if (A.Cost == 0) { A.Connections = 0; continue; }

				const FFixedPoint AStep = CellMaxStep[Y * GridW + X];

				uint8 Mask = 0;
				for (int32 n = 0; n < 8; ++n)
				{
					const int32 NX = X + DX8[n];
					const int32 NY = Y + DY8[n];
					if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;
					const FSeinAStarCell& B = OutAsset->Cells[NY * GridW + NX];
					if (B.Cost == 0) continue;

					++BakeEdges;

					// Midpoint surface trace.
					const float MidX = OriginWorld.X + (X + 0.5f + 0.5f * DX8[n]) * CellSizeF;
					const float MidY = OriginWorld.Y + (Y + 0.5f + 0.5f * DY8[n]) * CellSizeF;
					FHitResult MidHit;
					const FVector MidStart(MidX, MidY, TopZ);
					const FVector MidEnd(MidX, MidY, BottomZ);
					if (!World->LineTraceSingleByChannel(MidHit, MidStart, MidEnd, ECC_Visibility, QP))
					{
						++BakeBlockedEdges;
						continue; // no surface at midpoint → no edge
					}

					const FFixedPoint MidZ = FFixedPoint::FromFloat(MidHit.ImpactPoint.Z);
					const FFixedPoint AZ = A.Height;
					const FFixedPoint BZ = B.Height;

					// Half-step vertical deltas.
					const FFixedPoint AMid = (MidZ > AZ) ? (MidZ - AZ) : (AZ - MidZ);
					const FFixedPoint MidB = (BZ > MidZ) ? (BZ - MidZ) : (MidZ - BZ);

					// Max-step gate per half-step. Two adjacent cells can belong
					// to different volumes with different step rules — take the
					// min (most restrictive) so an edge survives only if BOTH
					// sides permit it.
					const FFixedPoint BStep = CellMaxStep[NY * GridW + NX];
					const FFixedPoint EdgeStep = (AStep < BStep) ? AStep : BStep;
					if (AMid > EdgeStep || MidB > EdgeStep)
					{
						++BakeBlockedEdges;
						continue;
					}

					// Slope gate on each half-step. Horizontal half-distance is
					// cellSize/2 for cardinal, cellSize/2 × √2 for diagonal — so
					// the squared half-distance is HalfCellSq or HalfDiagSq.
					const FFixedPoint HalfSq = (n < 4) ? HalfCellSq : HalfDiagSq;
					if ((AMid * AMid) > HalfSq * BakeMaxSlopeTanSq ||
					    (MidB * MidB) > HalfSq * BakeMaxSlopeTanSq)
					{
						++BakeBlockedEdges;
						continue;
					}

					Mask |= (1 << n);
				}
				A.Connections = Mask;
			}
		}

		UE_LOG(LogSeinNavAStar, Log, TEXT("Bake connectivity: edges=%d blocked_edges=%d FallbackStep=%.1f"),
			BakeEdges, BakeBlockedEdges, FallbackStepFP.ToFloat());
	}

	// ========================================================================
	// Connected-component pruning — isolated walkable regions (cube tops with
	// no bridge back to the main floor, floating geometry, etc.) are marked
	// blocked. Without this, GetGroundHeightAt on a cube-footprint cell would
	// return cube_top_Z and unit Z-snap would expose the cell's surface even
	// though pathing can't legitimately reach it. Also strips those cells from
	// the debug viz (they re-appear as red / unwalkable instead of green).
	// ========================================================================
	{
		static const int32 DX8[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
		static const int32 DY8[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };

		const int32 N = GridW * GridH;
		TArray<int32> Labels;
		Labels.Init(-1, N);
		TArray<int32> SizeByLabel;
		int32 NextLabel = 0;

		TArray<int32> BFSStack;
		BFSStack.Reserve(256);

		for (int32 Seed = 0; Seed < N; ++Seed)
		{
			if (OutAsset->Cells[Seed].Cost == 0) continue;
			if (Labels[Seed] != -1) continue;

			const int32 L = NextLabel++;
			int32 Count = 0;
			BFSStack.Reset();
			BFSStack.Add(Seed);
			Labels[Seed] = L;

			while (BFSStack.Num() > 0)
			{
				const int32 Cur = BFSStack.Pop(EAllowShrinking::No);
				++Count;

				const int32 CX = Cur % GridW;
				const int32 CY = Cur / GridW;
				const uint8 Conn = OutAsset->Cells[Cur].Connections;

				for (int32 n = 0; n < 8; ++n)
				{
					if ((Conn & (1 << n)) == 0) continue;
					const int32 NX = CX + DX8[n];
					const int32 NY = CY + DY8[n];
					if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;
					const int32 NIdx = NY * GridW + NX;
					if (Labels[NIdx] != -1) continue;
					Labels[NIdx] = L;
					BFSStack.Add(NIdx);
				}
			}

			SizeByLabel.Add(Count);
		}

		int32 LargestLabel = 0;
		int32 LargestSize = 0;
		for (int32 L = 0; L < SizeByLabel.Num(); ++L)
		{
			if (SizeByLabel[L] > LargestSize)
			{
				LargestSize = SizeByLabel[L];
				LargestLabel = L;
			}
		}

		int32 Pruned = 0;
		for (int32 i = 0; i < N; ++i)
		{
			if (Labels[i] != -1 && Labels[i] != LargestLabel)
			{
				OutAsset->Cells[i].Cost = 0;
				OutAsset->Cells[i].Connections = 0;
				++Pruned;
			}
		}

		UE_LOG(LogSeinNavAStar, Log,
			TEXT("Bake components: %d total, largest=%d cells, pruned %d isolated cells"),
			SizeByLabel.Num(), LargestSize, Pruned);

		// Per-component size histogram (up to 8 components logged) — helps
		// diagnose whether pruning actually isolated the cube top from the
		// ground, or whether a stray connection bit merged them.
		{
			FString Sizes;
			for (int32 L = 0; L < FMath::Min<int32>(SizeByLabel.Num(), 8); ++L)
			{
				Sizes += FString::Printf(TEXT("[%d]=%d%s "), L, SizeByLabel[L],
					L == LargestLabel ? TEXT("*") : TEXT(""));
			}
			if (SizeByLabel.Num() > 8) Sizes += TEXT("...");
			UE_LOG(LogSeinNavAStar, Log, TEXT("Bake component sizes: %s"), *Sizes);
		}

		// Update walkable/blocked tallies so the summary log reflects the post-
		// prune state.
		BakeWalkable = FMath::Max(0, BakeWalkable - Pruned);
		BakeBlocked += Pruned;
	}

	UE_LOG(LogSeinNavAStar, Log,
		TEXT("Bake stats: %dx%d=%d cells — walkable=%d blocked=%d (no_trace_hit=%d)"),
		GridW, GridH, GridW * GridH, BakeWalkable, BakeBlocked, BakeMissed);

#if WITH_EDITOR
	if (!SaveAssetToDisk(OutAsset))
	{
		UE_LOG(LogSeinNavAStar, Warning, TEXT("Bake: failed to save asset to disk"));
		// Asset still usable in-memory this session.
	}
#endif

	return true;
}

#if WITH_EDITOR
USeinNavigationAStarAsset* USeinNavigationAStar::CreateOrLoadAsset(UWorld* World, const FString& AssetName) const
{
	const FString PackagePath = FString::Printf(TEXT("/Game/NavData/%s"), *AssetName);
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) return nullptr;

	// Force full load before rewriting. UE's SavePackage refuses to save a
	// "partially loaded" package — which is the state we're in when (a) the
	// asset exists on disk from a previous bake and hasn't been fully pulled
	// into memory yet, or (b) the cell struct schema drifted (e.g., new
	// Connections field) since the last bake, leaving stale data in a
	// partial state. FullyLoad resolves both.
	Package->FullyLoad();

	USeinNavigationAStarAsset* Existing = FindObject<USeinNavigationAStarAsset>(Package, *AssetName);
	if (Existing) return Existing;

	USeinNavigationAStarAsset* Asset = NewObject<USeinNavigationAStarAsset>(
		Package, USeinNavigationAStarAsset::StaticClass(), FName(*AssetName),
		RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Asset);
	return Asset;
}

bool USeinNavigationAStar::SaveAssetToDisk(USeinNavigationAStarAsset* Asset) const
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
// LoadFromAsset
// ============================================================================

void USeinNavigationAStar::LoadFromAsset(USeinNavigationAsset* Asset)
{
	Super::LoadFromAsset(Asset);

	if (USeinNavigationAStarAsset* AStarAsset = Cast<USeinNavigationAStarAsset>(Asset))
	{
		ApplyAssetData(AStarAsset);
	}
	else
	{
		// Asset missing or wrong type — clear runtime state.
		Width = Height = 0;
		CellCost.Reset();
		CellHeight.Reset();
	}

	// Broadcast after runtime state is in sync — subscribers (debug scene proxy,
	// cached plan invalidation, etc.) see a consistent snapshot.
	OnNavigationMutated.Broadcast();
}

void USeinNavigationAStar::ApplyAssetData(const USeinNavigationAStarAsset* Asset)
{
	Width = Asset->Width;
	Height = Asset->Height;
	CellSize = Asset->CellSize;
	Origin = Asset->Origin;

	const int32 N = Width * Height;
	CellCost.SetNumUninitialized(N);
	CellHeight.SetNumUninitialized(N);
	CellConnections.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		CellCost[i] = Asset->Cells[i].Cost;
		CellHeight[i] = Asset->Cells[i].Height;
		CellConnections[i] = Asset->Cells[i].Connections;
	}

}

// ============================================================================
// Query helpers
// ============================================================================

bool USeinNavigationAStar::WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const
{
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
	const FFixedPoint LocalX = WorldPos.X - Origin.X;
	const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
	const int32 X = static_cast<int32>((LocalX / CellSize).ToFloat());
	const int32 Y = static_cast<int32>((LocalY / CellSize).ToFloat());
	if (!IsValidCoord(X, Y)) return false;
	OutX = X;
	OutY = Y;
	return true;
}

FFixedVector USeinNavigationAStar::GridToWorld(int32 X, int32 Y) const
{
	const FFixedPoint Half = CellSize / FFixedPoint::FromInt(2);
	const FFixedPoint WX = Origin.X + CellSize * FFixedPoint::FromInt(X) + Half;
	const FFixedPoint WY = Origin.Y + CellSize * FFixedPoint::FromInt(Y) + Half;
	const FFixedPoint WZ = IsValidCoord(X, Y) ? CellHeight[CellIndex(X, Y)] : Origin.Z;
	return FFixedVector(WX, WY, WZ);
}

bool USeinNavigationAStar::IsPassable(const FFixedVector& WorldPos) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y)) return false;
	return IsCellPassable(X, Y);
}

void USeinNavigationAStar::SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& InBlockers)
{
	DynamicBlockers = InBlockers;

	// Hash the new list so we only broadcast OnNavigationMutated when the
	// blocker set actually changed. The stamping system pushes every PreTick
	// regardless of motion; an unconditional broadcast here would invalidate
	// the debug scene proxy ~60×/sec for static scenes — wasted mesh builds.
	// XOR-fold is order-independent (matches the spatial-hash insertion-order
	// pattern) so the same set in any iteration order produces the same hash.
	uint32 NewHash = 0;
	for (const FSeinDynamicBlocker& B : InBlockers)
	{
		NewHash ^= GetTypeHash(B.Owner.Index);
		NewHash ^= GetTypeHash(B.EntityCenter.X.Value);
		NewHash ^= GetTypeHash(B.EntityCenter.Y.Value);
		NewHash ^= GetTypeHash(B.EntityCenter.Z.Value);
		NewHash ^= GetTypeHash(B.Shape);
		NewHash ^= static_cast<uint32>(B.BlockedNavLayerMask);
	}
	if (NewHash != LastBlockerHash)
	{
		LastBlockerHash = NewHash;
		OnNavigationMutated.Broadcast();
	}
}

void USeinNavigationAStar::BuildDynamicBlockedOverlay(FSeinEntityHandle Exclude, uint8 AgentNavLayerMask) const
{
	const int32 N = Width * Height;
	if (DynamicBlocked.Num() != N) DynamicBlocked.SetNumUninitialized(N);
	FMemory::Memzero(DynamicBlocked.GetData(), DynamicBlocked.Num());

	if (DynamicBlockers.Num() == 0 || N == 0) return;

	for (const FSeinDynamicBlocker& B : DynamicBlockers)
	{
		// Self-exclusion: a unit pathing out of its own footprint must not see
		// its own blocker stamped — A* would never find a start cell otherwise.
		if (B.Owner == Exclude) continue;

		// Layer-mask filter: blocker only affects this agent if their bits
		// intersect. Water (mask = Default) skips amphibious agent
		// (mask = N0) because the AND is zero.
		if ((B.BlockedNavLayerMask & AgentNavLayerMask) == 0) continue;

		// Shape iteration handles all three kinds (radial / rect / cone) plus
		// LocalOffset / YawOffset pose composition.
		SeinStampUtils::ForEachCoveredCell(
			B.Shape, B.EntityCenter, B.EntityRotation,
			CellSize, Origin, Width, Height,
			[this](int32 X, int32 Y)
			{
				DynamicBlocked[CellIndex(X, Y)] = 1;
			});
	}
}

bool USeinNavigationAStar::GetGroundHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y)) return false;
	// Refuse to report height for blocked cells — pruned cube tops, platform
	// interiors, and wall footprints all have their geometry's Z stored on
	// the cell. Without this gate, a unit whose XY momentarily crosses a
	// blocked cell (path smoother corner-cut, wheeled vehicle's turn radius
	// arc, footstep on a wall edge) Z-snaps to that blocked cell's surface
	// and visibly pops onto the wall. Returning false here makes locomotion
	// preserve the previous tick's Z (floor level), so the unit slides
	// through the blocked-cell sliver at its current height instead.
	if (!IsCellPassable(X, Y)) return false;
	OutZ = CellHeight[CellIndex(X, Y)];
	return true;
}

bool USeinNavigationAStar::ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
{
	if (!HasRuntimeData()) return false;

	int32 X, Y;
	if (WorldToGrid(WorldPos, X, Y) && IsCellPassable(X, Y))
	{
		OutProjected = GridToWorld(X, Y);
		return true;
	}

	// Simple radial scan outward for a walkable cell. Bounded by grid extent.
	const int32 MaxRing = FMath::Max(Width, Height);
	for (int32 R = 1; R <= MaxRing; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) continue;
				const int32 NX = X + dx, NY = Y + dy;
				if (IsCellPassable(NX, NY))
				{
					OutProjected = GridToWorld(NX, NY);
					return true;
				}
			}
		}
	}
	return false;
}

// ============================================================================
// A* pathfinding
// ============================================================================

namespace
{
	/** Octile distance * 10 (cardinal step = 10, diagonal = 14). Integer costs
	 *  for determinism. */
	FORCEINLINE int32 OctileHeuristic(int32 AX, int32 AY, int32 BX, int32 BY)
	{
		const int32 DX = FMath::Abs(AX - BX);
		const int32 DY = FMath::Abs(AY - BY);
		const int32 Mn = FMath::Min(DX, DY);
		const int32 Mx = FMath::Max(DX, DY);
		return 14 * Mn + 10 * (Mx - Mn);
	}

	struct FAStarNode
	{
		int32 CellIdx = 0;
		int32 FCost = 0;
		int32 GCost = 0;
		int32 Tiebreak = 0; // insertion order; keeps heap order deterministic

		bool operator<(const FAStarNode& Other) const
		{
			if (FCost != Other.FCost) return FCost < Other.FCost;
			if (GCost != Other.GCost) return GCost > Other.GCost; // prefer higher G (closer to goal)
			return Tiebreak < Other.Tiebreak;
		}
	};
}

TArray<FIntPoint> USeinNavigationAStar::AStarSearch(FIntPoint Start, FIntPoint End) const
{
	TArray<FIntPoint> Result;
	if (!IsValidCoord(Start.X, Start.Y) || !IsValidCoord(End.X, End.Y)) return Result;
	if (!IsCellPassableForPath(Start.X, Start.Y) || !IsCellPassableForPath(End.X, End.Y)) return Result;

	const int32 N = Width * Height;
	TArray<int32> GCosts; GCosts.Init(INT32_MAX, N);
	TArray<int32> Parents; Parents.Init(-1, N);
	TArray<uint8> Closed; Closed.Init(0, N);

	TArray<FAStarNode> Open;
	Open.Reserve(128);
	int32 Tiebreak = 0;

	const int32 StartIdx = CellIndex(Start.X, Start.Y);
	const int32 EndIdx = CellIndex(End.X, End.Y);

	GCosts[StartIdx] = 0;
	Open.HeapPush(FAStarNode{StartIdx, OctileHeuristic(Start.X, Start.Y, End.X, End.Y), 0, Tiebreak++});

	static const int32 NeighborDX[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
	static const int32 NeighborDY[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };
	static const int32 NeighborCost[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };

	while (Open.Num() > 0)
	{
		FAStarNode Cur;
		Open.HeapPop(Cur, EAllowShrinking::No);

		if (Closed[Cur.CellIdx]) continue;
		Closed[Cur.CellIdx] = 1;

		if (Cur.CellIdx == EndIdx)
		{
			// Reconstruct cell path.
			TArray<FIntPoint> Reverse;
			int32 CurIdx = EndIdx;
			while (CurIdx != -1)
			{
				Reverse.Add(FIntPoint(CurIdx % Width, CurIdx / Width));
				CurIdx = Parents[CurIdx];
			}
			Result.Reserve(Reverse.Num());
			for (int32 i = Reverse.Num() - 1; i >= 0; --i) Result.Add(Reverse[i]);
			return Result;
		}

		const int32 CX = Cur.CellIdx % Width;
		const int32 CY = Cur.CellIdx / Width;

		const uint8 CurConn = CellConnections[Cur.CellIdx];

		for (int32 n = 0; n < 8; ++n)
		{
			// Connectivity bit — set at bake time via midpoint trace + slope +
			// max-step gate. Replaces live slope math in the hot loop.
			if ((CurConn & (1 << n)) == 0) continue;

			const int32 NX = CX + NeighborDX[n];
			const int32 NY = CY + NeighborDY[n];
			if (!IsCellPassableForPath(NX, NY)) continue; // static + dynamic blocker check

			const int32 NIdx = CellIndex(NX, NY);

			// Disallow diagonal squeezes through blocked-or-disconnected corners.
			// The diagonal bit being set on THIS cell doesn't imply both cardinal
			// transitions are legal — check each cardinal-step bit on the current
			// cell's connectivity mask.
			if (n >= 4)
			{
				// Cardinal indices making up this diagonal:
				//   4: (+1,+1) → cardinal (+1,0)=idx 0 and (0,+1)=idx 2
				//   5: (+1,-1) → (+1,0)=0, (0,-1)=3
				//   6: (-1,+1) → (-1,0)=1, (0,+1)=2
				//   7: (-1,-1) → (-1,0)=1, (0,-1)=3
				static const uint8 CardinalA[4] = { 0, 0, 1, 1 };
				static const uint8 CardinalB[4] = { 2, 3, 2, 3 };
				const uint8 AIdx = CardinalA[n - 4];
				const uint8 BIdx = CardinalB[n - 4];
				if ((CurConn & (1 << AIdx)) == 0 || (CurConn & (1 << BIdx)) == 0) continue;
			}

			if (Closed[NIdx]) continue;

			const int32 StepCost = NeighborCost[n] * CellCost[NIdx];
			const int32 NewG = Cur.GCost + StepCost;
			if (NewG >= GCosts[NIdx]) continue;

			GCosts[NIdx] = NewG;
			Parents[NIdx] = Cur.CellIdx;
			const int32 H = OctileHeuristic(NX, NY, End.X, End.Y);
			Open.HeapPush(FAStarNode{NIdx, NewG + H, NewG, Tiebreak++});
		}
	}

	return Result; // empty = no path
}

// ============================================================================
// Path smoothing (line-of-sight string-pull)
// ============================================================================

bool USeinNavigationAStar::HasLineOfSight(int32 X0, int32 Y0, int32 X1, int32 Y1) const
{
	// Bresenham supercover with connectivity gate — the smoother must honor the
	// same rules A* uses, otherwise a path that A* carefully routed around an
	// obstacle gets collapsed into a straight line that walks through
	// walkable-but-disconnected cells (e.g., across a cube footprint).
	int32 DX = FMath::Abs(X1 - X0);
	int32 DY = FMath::Abs(Y1 - Y0);
	int32 SX = (X0 < X1) ? 1 : -1;
	int32 SY = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX - DY;

	int32 X = X0, Y = Y0;

	while (true)
	{
		if (!IsCellPassableForPath(X, Y)) return false;
		if (X == X1 && Y == Y1) return true;

		int32 NextX = X, NextY = Y;
		const int32 E2 = 2 * Err;
		if (E2 > -DY) { Err -= DY; NextX += SX; }
		if (E2 < DX)  { Err += DX; NextY += SY; }

		// Connectivity gate on the step about to be taken. Map the (dx, dy)
		// direction to the 8-neighbor bitmask index used at bake time.
		const int32 StepDX = NextX - X;
		const int32 StepDY = NextY - Y;
		int32 DirIdx = -1;
		if (StepDX ==  1 && StepDY ==  0) DirIdx = 0;
		else if (StepDX == -1 && StepDY ==  0) DirIdx = 1;
		else if (StepDX ==  0 && StepDY ==  1) DirIdx = 2;
		else if (StepDX ==  0 && StepDY == -1) DirIdx = 3;
		else if (StepDX ==  1 && StepDY ==  1) DirIdx = 4;
		else if (StepDX ==  1 && StepDY == -1) DirIdx = 5;
		else if (StepDX == -1 && StepDY ==  1) DirIdx = 6;
		else if (StepDX == -1 && StepDY == -1) DirIdx = 7;

		if (DirIdx >= 0)
		{
			const uint8 Conn = CellConnections[CellIndex(X, Y)];
			if ((Conn & (1 << DirIdx)) == 0) return false;
			// Diagonal anti-squeeze — mirrors A*'s rule. The diagonal
			// connectivity bit alone says "midpoint trace passed slope+step
			// gates," but a paired-cardinal blockage (the two cells the
			// diagonal step skims past) means the line physically crosses
			// blocked terrain even if the bake's midpoint sample was happy.
			// Without this, path smoothing can collapse around wall corners
			// A* carefully routed around — visible as paths cutting through
			// red-blocked cells at platform edges.
			if (DirIdx >= 4)
			{
				static const uint8 CardinalA[4] = { 0, 0, 1, 1 };
				static const uint8 CardinalB[4] = { 2, 3, 2, 3 };
				const uint8 AIdx = CardinalA[DirIdx - 4];
				const uint8 BIdx = CardinalB[DirIdx - 4];
				if ((Conn & (1 << AIdx)) == 0 || (Conn & (1 << BIdx)) == 0) return false;
			}
		}

		X = NextX;
		Y = NextY;
	}
}

void USeinNavigationAStar::BuildSmoothedPath(const TArray<FIntPoint>& CellPath, FSeinPath& OutPath) const
{
	OutPath.Clear();
	if (CellPath.Num() == 0) return;

	// String-pull from cellPath[0] onward: advance J as far as LoS(anchor, J+1) holds,
	// commit cellPath[J] as a turn point, anchor = J, repeat. We deliberately DO NOT
	// emit cellPath[0] — the unit already occupies that cell, and emitting its center
	// as the first waypoint creates a visible "hook" where the unit snaps to the cell
	// center before heading to the real destination.
	OutPath.Waypoints.Reserve(CellPath.Num());

	int32 I = 0;
	while (I < CellPath.Num() - 1)
	{
		int32 J = I + 1;
		while (J + 1 < CellPath.Num() &&
			HasLineOfSight(CellPath[I].X, CellPath[I].Y, CellPath[J + 1].X, CellPath[J + 1].Y))
		{
			++J;
		}
		OutPath.Waypoints.Add(GridToWorld(CellPath[J].X, CellPath[J].Y));
		I = J;
	}

	// Same-cell path (CellPath.Num() == 1): loop above emits nothing, but we still
	// need one waypoint so FindPath can overwrite it with the exact destination.
	if (OutPath.Waypoints.Num() == 0)
	{
		OutPath.Waypoints.Add(GridToWorld(CellPath.Last().X, CellPath.Last().Y));
	}

	OutPath.bIsValid = true;
}

// ============================================================================
// FindPath
// ============================================================================

bool USeinNavigationAStar::FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const
{
	OutPath.Clear();
	if (!HasRuntimeData()) return false;

	// Rebuild the dynamic-blocker overlay for this request. Excludes the
	// requesting entity's own blocker so a unit can path out of its own
	// footprint, AND filters by agent layer mask so terrain-class blockers
	// (water blocks Default, ignore amphibious) don't apply universally.
	// Done first so every passability check below — source projection, A*,
	// smoothing — sees the same overlay.
	BuildDynamicBlockedOverlay(Request.Requester, Request.AgentNavLayerMask);

	int32 SX, SY;
	if (!WorldToGrid(Request.Start, SX, SY))
	{
		UE_LOG(LogSeinNavAStar, Verbose, TEXT("FindPath: start out of bounds"));
		return false;
	}
	int32 EX, EY;
	if (!WorldToGrid(Request.End, EX, EY))
	{
		UE_LOG(LogSeinNavAStar, Verbose, TEXT("FindPath: end out of bounds"));
		return false;
	}

	// Source projection: if the unit's physical cell is blocked (e.g. stuck on
	// a pruned island from a stale bake, or boxed in by other units' blocker
	// stamps), ring-scan for a nearby walkable cell. Uses static-only
	// passability — ProjectPointToNav doesn't see the dynamic overlay; if it
	// did, a tank surrounded by other blockers couldn't even start a path.
	if (!IsCellPassable(SX, SY))
	{
		FFixedVector Snapped;
		if (!ProjectPointToNav(Request.Start, Snapped)) return false;
		WorldToGrid(Snapped, SX, SY);
	}

	// Destinations on blocked cells (pruned cube tops, out-of-nav, etc.) skip
	// A* and fall into the Bresenham walkback below. Designers who want
	// "reject invalid destination instead of falling back" flip the ability's
	// bRequiresPathableTarget flag — that routes through IsReachable at
	// command-validation time, before FindPath is ever called. So FindPath
	// itself is always permissive: the MoveToAction's contract is to get
	// somewhere sensible along the straight line toward the click.
	TArray<FIntPoint> CellPath;
	if (IsCellPassableForPath(EX, EY))
	{
		CellPath = AStarSearch(FIntPoint(SX, SY), FIntPoint(EX, EY));
	}

	// Fallback: if A* can't reach the destination (unreachable without a ramp —
	// e.g., clicking on top of a cube with no slope up), walk Bresenham from the
	// destination back toward the source and try A* to each cell along the line.
	// First reachable cell wins; the path is marked partial. "Nearest cell on
	// the floor to the destination cell that lies along a straight path."
	//
	// NOTE: we track partial locally rather than setting OutPath.bIsPartial
	// here directly — BuildSmoothedPath below calls OutPath.Clear() which
	// would reset the flag. Apply to OutPath *after* smoothing.
	bool bPartial = false;
	if (CellPath.Num() == 0)
	{
		int32 X = EX, Y = EY;
		int32 DX = FMath::Abs(SX - EX);
		int32 DY = FMath::Abs(SY - EY);
		int32 SXs = (EX < SX) ? 1 : -1;
		int32 SYs = (EY < SY) ? 1 : -1;
		int32 Err = DX - DY;

		// Bounded scan — worst case grid diagonal, plus a small slack.
		const int32 MaxSteps = Width + Height + 4;
		int32 Steps = 0;
		while (Steps++ < MaxSteps)
		{
			if (X == SX && Y == SY) break;
			const int32 E2 = 2 * Err;
			if (E2 > -DY) { Err -= DY; X += SXs; }
			if (E2 < DX)  { Err += DX; Y += SYs; }

			if (!IsValidCoord(X, Y) || !IsCellPassableForPath(X, Y)) continue;

			TArray<FIntPoint> Candidate = AStarSearch(FIntPoint(SX, SY), FIntPoint(X, Y));
			if (Candidate.Num() > 0)
			{
				CellPath = MoveTemp(Candidate);
				EX = X;
				EY = Y;
				bPartial = true;
				break;
			}
		}
	}

	if (CellPath.Num() == 0) return false;

	BuildSmoothedPath(CellPath, OutPath);
	// Apply partial flag AFTER smoothing — BuildSmoothedPath's OutPath.Clear()
	// resets it, so setting it earlier gets clobbered and the "arrive at exact
	// click" replacement below would then incorrectly snap the final waypoint
	// onto the original unreachable destination (e.g., a cube top).
	OutPath.bIsPartial = bPartial;

	// Replace last waypoint with the requested end position (within the cell)
	// so unit actually arrives at the ordered point, not the cell center.
	// Partial paths keep the cell-center final waypoint — the destination is
	// literally unreachable, so snapping to it would put the unit inside a wall.
	if (OutPath.Waypoints.Num() > 0 && !OutPath.bIsPartial)
	{
		OutPath.Waypoints.Last() = Request.End;
	}
	return OutPath.bIsValid;
}

// ============================================================================
// Debug geometry collectors. Bodies compiled out of shipping via
// UE_ENABLE_DEBUG_DRAWING; declarations stay in the header so the vtable /
// override signature is ABI-stable across build configs.
// ============================================================================

void USeinNavigationAStar::CollectDebugCellQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	OutCenters.Reset();
	OutColors.Reset();
	OutHalfExtent = 0.0f;
	if (!bDrawCellsInDebug || !HasRuntimeData()) return;

	const float CS = CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f; // small inset so neighboring quads don't z-fight

	const int32 N = Width * Height;
	OutCenters.Reserve(N);
	OutColors.Reserve(N);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 I = CellIndex(X, Y);
			const uint8 C = CellCost[I];
			const bool bWalkable = C > 0 && C < 255;
			OutColors.Add(bWalkable ? FColor(0, 200, 0, 160) : FColor(200, 0, 0, 200));
			OutCenters.Emplace(
				Origin.X.ToFloat() + (X + 0.5f) * CS,
				Origin.Y.ToFloat() + (Y + 0.5f) * CS,
				CellHeight[I].ToFloat() + 2.0f);
		}
	}
#endif
}

void USeinNavigationAStar::CollectDebugBlockerCells(
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	UE_LOG(LogSeinNavAStar, Verbose,
		TEXT("CollectDebugBlockerCells: ENTERED (HasRuntimeData=%d, DynamicBlockers=%d)"),
		HasRuntimeData() ? 1 : 0, DynamicBlockers.Num());

	OutCenters.Reset();
	OutColors.Reset();
	OutHalfExtent = 0.0f;
	if (!HasRuntimeData() || DynamicBlockers.Num() == 0) return;

	const float CS = CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f;

	// Dedup cells when multiple blockers overlap — without this an overlapping
	// pair stamps shared cells twice and the alpha stacks, which reads as
	// "this cell is double-blocked" when really it's just the viz folding
	// two stamps onto one cell. First-blocker-wins for the color (rare
	// overlap case).
	TArray<bool> Stamped;
	Stamped.Init(false, Width * Height);

	const float OriginXF = Origin.X.ToFloat();
	const float OriginYF = Origin.Y.ToFloat();

	// Layer-perspective filter from the `Sein.Nav.Show.Layer` console command.
	// When set, blockers whose BlockedNavLayerMask doesn't intersect the
	// filter bit are skipped entirely AND the visible blockers all render
	// in the filter layer's color (uniform "this is what blocks layer X" viz).
	// When no filter, each blocker renders in its OWN dominant-bit color
	// (lowest set bit) — so a Default-only blocker shows red, an N0 blocker
	// shows N0's settings color, etc. This matches the user mental model
	// where Sein.Nav.Show and Sein.Nav.Show.Layer 0 produce identical output
	// when every blocker happens to be on the Default layer.
	int32 FilterBit = -1;
	const bool bLayerFilter = UE::SeinARTSNavigation::TryGetDebugNavLayerOverride(FilterBit);
	const uint8 FilterBitMask = bLayerFilter ? static_cast<uint8>(1u << FilterBit) : uint8(0xFF);

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();

	auto ColorForBit = [&](int32 Bit) -> FColor
	{
		// Bit 0 (Default) → red, matching the static blocker red so dynamic
		// and static "Default-blocking" cells visually merge.
		if (Bit == 0) return FColor(217, 0, 0, 128);
		if (Bit >= 1 && Bit <= 7 && Settings && Settings->NavLayers.IsValidIndex(Bit - 1))
		{
			FLinearColor C = Settings->NavLayers[Bit - 1].DebugColor;
			C.A = 0.5f;
			return C.ToFColor(true);
		}
		// Fallback (no settings entry) — catch-all orange.
		return FColor(255, 140, 0, 128);
	};

	const FColor FilterColor = bLayerFilter ? ColorForBit(FilterBit) : FColor::Magenta;

	for (const FSeinDynamicBlocker& B : DynamicBlockers)
	{
		if (bLayerFilter && (B.BlockedNavLayerMask & FilterBitMask) == 0) continue;

		// Resolve cell color for this blocker:
		//   filter active → uniform filter color (everything visible affects
		//                    that layer, render in its color).
		//   no filter     → blocker's own dominant-bit color (lowest set bit).
		FColor BlockerColor;
		if (bLayerFilter)
		{
			BlockerColor = FilterColor;
		}
		else
		{
			int32 LowestBit = 0;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				if (B.BlockedNavLayerMask & (1u << Bit)) { LowestBit = Bit; break; }
			}
			BlockerColor = ColorForBit(LowestBit);
		}

		// One pass per stamp. Same cell-iteration helper used by the FindPath
		// overlay rebuild — viz exactly matches what pathing sees.
		SeinStampUtils::ForEachCoveredCell(
			B.Shape, B.EntityCenter, B.EntityRotation,
			CellSize, Origin, Width, Height,
			[&](int32 X, int32 Y)
			{
				const int32 Idx = CellIndex(X, Y);
				if (Stamped[Idx]) return;
				Stamped[Idx] = true;
				const float CellCX = OriginXF + (X + 0.5f) * CS;
				const float CellCY = OriginYF + (Y + 0.5f) * CS;
				// +15cm Z bias so blocker quads layer cleanly above the
				// static cell quads (which render at +2cm).
				OutCenters.Emplace(CellCX, CellCY, CellHeight[Idx].ToFloat() + 15.0f);
				OutColors.Add(BlockerColor);
			});
	}

	UE_LOG(LogSeinNavAStar, Verbose,
		TEXT("CollectDebugBlockerCells: %d blocker(s) → %d unique cell(s) (filter=%d)"),
		DynamicBlockers.Num(), OutCenters.Num(), bLayerFilter ? FilterBit : -1);
#endif
}

void USeinNavigationAStar::CollectDebugPathCells(
	const FFixedVector& AgentPos,
	const TArray<FFixedVector>& Waypoints,
	int32 CurrentWaypointIndex,
	TArray<FVector>& OutRemainingCells,
	TArray<FVector>& OutCurrentTargetCell,
	float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	OutRemainingCells.Reset();
	OutCurrentTargetCell.Reset();
	OutHalfExtent = 0.0f;
	if (!HasRuntimeData() || !Waypoints.IsValidIndex(CurrentWaypointIndex)) return;

	const float CS = CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f;

	auto CellCenterFVec = [this](int32 X, int32 Y)
	{
		const FFixedVector V = GridToWorld(X, Y);
		return FVector(V.X.ToFloat(), V.Y.ToFloat(), V.Z.ToFloat());
	};

	// Final waypoint = the order's end point, drawn as the highlighted
	// destination cell. Excluded from the path list below so there's no
	// double-draw.
	const int32 FinalIdx = Waypoints.Num() - 1;
	int32 DestX = INT32_MIN, DestY = INT32_MIN;
	if (Waypoints.IsValidIndex(FinalIdx) && WorldToGrid(Waypoints[FinalIdx], DestX, DestY))
	{
		OutCurrentTargetCell.Add(CellCenterFVec(DestX, DestY));
	}

	const int32 SkipIndex = (DestX != INT32_MIN) ? CellIndex(DestX, DestY) : -1;

	TSet<int32> Visited;
	Visited.Reserve(64);

	auto VisitCell = [&](int32 X, int32 Y)
	{
		if (!IsValidCoord(X, Y)) return;
		const int32 Idx = CellIndex(X, Y);
		if (Idx == SkipIndex) return; // drawn separately as destination
		bool bAlreadyIn = false;
		Visited.Add(Idx, &bAlreadyIn);
		if (bAlreadyIn) return;
		OutRemainingCells.Add(CellCenterFVec(X, Y));
	};

	auto Rasterize = [&](int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		const int32 DX = FMath::Abs(X1 - X0);
		const int32 DY = FMath::Abs(Y1 - Y0);
		const int32 SX = (X0 < X1) ? 1 : -1;
		const int32 SY = (Y0 < Y1) ? 1 : -1;
		int32 Err = DX - DY;
		int32 X = X0, Y = Y0;
		for (;;)
		{
			VisitCell(X, Y);
			if (X == X1 && Y == Y1) return;
			const int32 E2 = 2 * Err;
			if (E2 > -DY) { Err -= DY; X += SX; }
			if (E2 < DX)  { Err += DX; Y += SY; }
		}
	};

	// Start from the agent's current cell (or the current waypoint if the
	// agent has drifted out of bounds), then walk through every remaining
	// waypoint. Bresenham fills in all grid cells the line segments cross.
	int32 AgentX, AgentY;
	const bool bAgentInGrid = WorldToGrid(AgentPos, AgentX, AgentY);

	int32 PrevX = INT32_MIN, PrevY = INT32_MIN;
	if (bAgentInGrid)
	{
		PrevX = AgentX;
		PrevY = AgentY;
	}
	else if (WorldToGrid(Waypoints[CurrentWaypointIndex], PrevX, PrevY))
	{
		// fell back to current waypoint
	}
	else
	{
		return;
	}

	for (int32 i = CurrentWaypointIndex; i < Waypoints.Num(); ++i)
	{
		int32 NX, NY;
		if (!WorldToGrid(Waypoints[i], NX, NY)) break;
		Rasterize(PrevX, PrevY, NX, NY);
		PrevX = NX;
		PrevY = NY;
	}
#endif
}
