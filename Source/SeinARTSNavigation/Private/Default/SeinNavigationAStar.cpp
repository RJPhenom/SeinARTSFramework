/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStar.cpp
 */

#include "Default/SeinNavigationAStar.h"
#include "Default/SeinNavigationAStarAsset.h"
#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"

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
	const float CellSizeF = Volumes[0]->GetResolvedCellSize();
	const FFixedPoint BakedCellSize = FFixedPoint::FromFloat(CellSizeF);

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

			// Primary trace: first blocker from above. Could be ground OR an
			// obstacle sitting on the ground (a single line trace stops at the
			// first blocking collider, so by itself it can't distinguish).
			FHitResult TopHit;
			if (!World->LineTraceSingleByChannel(TopHit, Start, End, ECC_WorldStatic, QP))
			{
				Cell.Cost = 0;
				Cell.Height = FFixedPoint::FromFloat(BottomZ);
			}
			else
			{
				// Secondary trace: ignore the first-hit actor and see if there's
				// ground beneath it. If the deeper hit is meaningfully lower, the
				// first actor is an obstacle above the ground → cell is blocked,
				// but stored at the *ground* height so the debug viz sits flush
				// with the floor (not hovering on top of the obstacle).
				FHitResult GroundHit = TopHit;
				bool bObstacle = false;
				if (AActor* TopActor = TopHit.GetActor())
				{
					FCollisionQueryParams QP2 = QP;
					QP2.AddIgnoredActor(TopActor);
					FHitResult BelowHit;
					if (World->LineTraceSingleByChannel(BelowHit, Start, End, ECC_WorldStatic, QP2))
					{
						if (BelowHit.ImpactPoint.Z < TopHit.ImpactPoint.Z - 10.0f)
						{
							GroundHit = BelowHit;
							bObstacle = true;
						}
					}
				}

				const float Dot = FVector::DotProduct(GroundHit.Normal, FVector::UpVector);
				const bool bSlopeOK = Dot >= MaxSlopeCos;
				Cell.Cost = (bSlopeOK && !bObstacle) ? 1 : 0;
				Cell.Height = FFixedPoint::FromFloat(GroundHit.ImpactPoint.Z);
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
	for (int32 i = 0; i < N; ++i)
	{
		CellCost[i] = Asset->Cells[i].Cost;
		CellHeight[i] = Asset->Cells[i].Height;
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
	if (!IsCellPassable(Start.X, Start.Y) || !IsCellPassable(End.X, End.Y)) return Result;

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

		for (int32 n = 0; n < 8; ++n)
		{
			const int32 NX = CX + NeighborDX[n];
			const int32 NY = CY + NeighborDY[n];
			if (!IsCellPassable(NX, NY)) continue;

			// Disallow diagonal squeezes through blocked corners.
			if (n >= 4)
			{
				if (!IsCellPassable(CX + NeighborDX[n], CY) || !IsCellPassable(CX, CY + NeighborDY[n]))
					continue;
			}

			const int32 NIdx = CellIndex(NX, NY);
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
	// Bresenham supercover — steps through every cell the line touches.
	int32 DX = FMath::Abs(X1 - X0);
	int32 DY = FMath::Abs(Y1 - Y0);
	int32 SX = (X0 < X1) ? 1 : -1;
	int32 SY = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX - DY;

	int32 X = X0, Y = Y0;
	while (true)
	{
		if (!IsCellPassable(X, Y)) return false;
		if (X == X1 && Y == Y1) return true;
		const int32 E2 = 2 * Err;
		if (E2 > -DY) { Err -= DY; X += SX; }
		if (E2 < DX)  { Err += DX; Y += SY; }
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

	// Project unreachable start/end to nearest walkable cell.
	if (!IsCellPassable(SX, SY))
	{
		FFixedVector Snapped;
		if (!ProjectPointToNav(Request.Start, Snapped)) return false;
		WorldToGrid(Snapped, SX, SY);
	}
	if (!IsCellPassable(EX, EY))
	{
		FFixedVector Snapped;
		if (!ProjectPointToNav(Request.End, Snapped)) return false;
		WorldToGrid(Snapped, EX, EY);
		OutPath.bIsPartial = true;
	}

	const TArray<FIntPoint> CellPath = AStarSearch(FIntPoint(SX, SY), FIntPoint(EX, EY));
	if (CellPath.Num() == 0) return false;

	BuildSmoothedPath(CellPath, OutPath);

	// Replace last waypoint with the requested end position (within the cell)
	// so unit actually arrives at the ordered point, not the cell center.
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
