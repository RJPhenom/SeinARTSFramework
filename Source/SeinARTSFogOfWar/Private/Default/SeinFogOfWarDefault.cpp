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
#include "SeinFogOfWarTypes.h"

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
	// supports per-region grids).
	const float CellSizeF = Volumes[0]->GetResolvedCellSize();
	const FFixedPoint BakedCellSize = FFixedPoint::FromFloat(CellSizeF);
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
				// on comes from runtime USeinVisionBlockerComponent overlays.

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
		VisionGroups.Empty();
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
	VisionGroups.Empty(); // per-player state recreates lazily on next stamp

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

	// Union all fog volume bounds, min of cell sizes.
	FBox UnionBounds(ForceInit);
	float MinCellSize = FLT_MAX;
	int32 VolumeCount = 0;
	for (TActorIterator<ASeinFogOfWarVolume> It(World); It; ++It)
	{
		const FBox VB = It->GetVolumeWorldBounds();
		if (!VB.IsValid) continue;
		if (!UnionBounds.IsValid) UnionBounds = VB;
		else UnionBounds += VB;
		MinCellSize = FMath::Min(MinCellSize, It->GetResolvedCellSize());
		++VolumeCount;
	}
	if (VolumeCount == 0 || !UnionBounds.IsValid || MinCellSize >= FLT_MAX)
	{
		Width = 0;
		Height = 0;
		return;
	}

	CellSize = FFixedPoint::FromFloat(MinCellSize);
	Origin = FFixedVector(
		FFixedPoint::FromFloat(UnionBounds.Min.X),
		FFixedPoint::FromFloat(UnionBounds.Min.Y),
		FFixedPoint::FromFloat(UnionBounds.Min.Z));
	const float CellSizeF = MinCellSize;
	Width = FMath::Max(1, FMath::CeilToInt(UnionBounds.GetSize().X / CellSizeF));
	Height = FMath::Max(1, FMath::CeilToInt(UnionBounds.GetSize().Y / CellSizeF));

	const int32 NumCells = Width * Height;
	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumZeroed(NumCells);
	BlockerLayerMask.SetNumZeroed(NumCells);
	VisionGroups.Empty();

	// Per-cell downward trace capped at InitTraceCellCap — no-bake fallback.
	const bool bTracePerCell = NumCells <= InitTraceCellCap;
	const FFixedPoint FallbackZ = FFixedPoint::FromFloat((UnionBounds.Min.Z + UnionBounds.Max.Z) * 0.5f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinFowInitTrace), /*bTraceComplex*/ true);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float CX = UnionBounds.Min.X + (X + 0.5f) * CellSizeF;
			const float CY = UnionBounds.Min.Y + (Y + 0.5f) * CellSizeF;
			FFixedPoint Z = FallbackZ;
			if (bTracePerCell)
			{
				FHitResult Hit;
				const FVector Start(CX, CY, UnionBounds.Max.Z + 100.0f);
				const FVector End  (CX, CY, UnionBounds.Min.Z - 100.0f);
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

	// Clear V bits in every existing group but preserve Explored — that bit
	// is sticky per-player-per-match (classic fog-of-war memory).
	for (TPair<FSeinPlayerID, FSeinFogVisionGroup>& Pair : VisionGroups)
	{
		for (uint8& Byte : Pair.Value.CellBitfield)
		{
			Byte &= SEIN_FOW_BIT_EXPLORED;
		}
	}

	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Storage](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinVisionData* VData = static_cast<const FSeinVisionData*>(Raw);
			if (!VData) return;
			if (VData->VisionRadius <= FFixedPoint::Zero) return;

			const FSeinPlayerID OwnerPlayer = Sim->GetEntityOwner(Handle);
			StampShadowcast(OwnerPlayer, Entity.Transform.GetLocation(),
				VData->VisionRadius, VData->EyeHeight);
		});

	OnFogOfWarMutated.Broadcast();
}

void USeinFogOfWarDefault::StampShadowcast(FSeinPlayerID OwnerPlayer,
	const FFixedVector& WorldPos, FFixedPoint Radius, FFixedPoint EyeHeight)
{
	int32 SX, SY;
	if (!WorldToGrid(WorldPos, SX, SY)) return;
	if (CellSize <= FFixedPoint::Zero || Radius <= FFixedPoint::Zero) return;

	// Source eye Z in absolute world coords. GroundHeight at the source's
	// own cell is the reference; the unit is treated as standing on that
	// cell's ground regardless of its authored Z (keeps sim/render decoupled
	// and lampshade test deterministic).
	const int32 SrcIdx = CellIndex(SX, SY);
	const FFixedPoint SourceGroundZ = GroundHeight.IsValidIndex(SrcIdx) ? GroundHeight[SrcIdx] : Origin.Z;
	const FFixedPoint EyeZ = SourceGroundZ + EyeHeight;

	// Grab the owner's group once — stamps write into this bitfield only.
	TArray<uint8>& OwnerBits = GetOrCreateGroup(OwnerPlayer).CellBitfield;

	// Radius bounds in cell units — see StampFlatCircle history in git for
	// the integer-ceil-of-a-fixed-point explainer.
	const FFixedPoint RadiusCellsFP = Radius / CellSize;
	const int32 RadiusCellsFloor = RadiusCellsFP.ToInt();
	const bool bHasFraction = (RadiusCellsFP.Value & FFixedPoint::FractionalMask) != 0;
	const int32 RadiusCells = bHasFraction ? RadiusCellsFloor + 1 : RadiusCellsFloor;
	const FFixedPoint RadiusCellsSq = RadiusCellsFP * RadiusCellsFP;

	const uint8 StampMask = SEIN_FOW_BIT_NORMAL | SEIN_FOW_BIT_EXPLORED;

	// Source's own cell is always visible.
	OwnerBits[SrcIdx] |= StampMask;

	// Iterate the radius bounding box; for each cell inside the circular
	// radius, Bresenham-walk from source and stamp only if no intermediate
	// cell is opaque to our eye.
	for (int32 DY = -RadiusCells; DY <= RadiusCells; ++DY)
	{
		const int32 TY = SY + DY;
		if (TY < 0 || TY >= Height) continue;
		const int32 DYSq = DY * DY;
		for (int32 DX = -RadiusCells; DX <= RadiusCells; ++DX)
		{
			if (DX == 0 && DY == 0) continue;
			const FFixedPoint DistSqCells = FFixedPoint::FromInt(DX * DX + DYSq);
			if (DistSqCells > RadiusCellsSq) continue;
			const int32 TX = SX + DX;
			if (TX < 0 || TX >= Width) continue;

			if (HasLineOfSightToCell(SX, SY, TX, TY, EyeZ))
			{
				OwnerBits[CellIndex(TX, TY)] |= StampMask;
			}
		}
	}
}

bool USeinFogOfWarDefault::HasLineOfSightToCell(int32 X0, int32 Y0, int32 X1, int32 Y1, FFixedPoint EyeZ) const
{
	if (X0 == X1 && Y0 == Y1) return true;

	// Classic Bresenham — integer-native, symmetric, same walk from either
	// endpoint. Source cell is the starting point (not tested); target cell
	// is whatever we're trying to see (not tested — you can always "see"
	// the thing you're looking at, even if it's a wall). Opacity test
	// applies to every cell in between.
	const int32 DX =  FMath::Abs(X1 - X0);
	const int32 DY = -FMath::Abs(Y1 - Y0);
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

		if (X == X1 && Y == Y1) return true;
		if (IsCellOpaqueToEye(X, Y, EyeZ)) return false;
	}
}

bool USeinFogOfWarDefault::IsCellOpaqueToEye(int32 X, int32 Y, FFixedPoint EyeZ) const
{
	if (!IsValidCoord(X, Y)) return false;
	const int32 Idx = CellIndex(X, Y);
	const FFixedPoint GroundZ  = GroundHeight.IsValidIndex(Idx)  ? GroundHeight[Idx]  : FFixedPoint::Zero;
	const FFixedPoint BlockerZ = BlockerHeight.IsValidIndex(Idx) ? BlockerHeight[Idx] : FFixedPoint::Zero;
	// Effective blocker top = max(ground, blocker) — open terrain uses
	// ground (hills block distant sight); cells with structures use the
	// blocker top.
	const FFixedPoint Top = (BlockerZ > GroundZ) ? BlockerZ : GroundZ;
	return Top > EyeZ;
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

void USeinFogOfWarDefault::CollectDebugCellQuads(FSeinPlayerID Observer,
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
	if (Width <= 0 || Height <= 0) return;

	// 0.9 inset → ~10% gap between neighboring quads. Reads as a grid (matches
	// USeinNavigationAStar::CollectDebugCellQuads). Also dodges any z-fight at
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
	// there's no group — treat every cell's V bit as 0 (non-PIE / editor
	// preview / unknown observer all render as baseline).
	const FSeinFogVisionGroup* ObserverGroup = VisionGroups.Find(Observer);
	const TArray<uint8>* ObserverBits = ObserverGroup ? &ObserverGroup->CellBitfield : nullptr;

	// Three-state color scheme (debug proxy buckets by channel pattern):
	//  cyan  → cell is visible this tick (V bit set for Observer)
	//  red   → cell is a baked static blocker (no vision this tick)
	//  black → cell is in-grid with no blocker + no visibility
	// Cell Z: blocker cells draw at blocker-top (shows the blocker's height);
	// everything else draws at ground height. Matches nav's per-cell-Z pattern.
	const FColor Cyan (0, 200, 255);
	const FColor Red  (200, 0, 0);
	const FColor Black(0, 0, 0);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 Bits = (ObserverBits && ObserverBits->IsValidIndex(Idx)) ? (*ObserverBits)[Idx] : 0;
			const bool bVisible = (Bits & SEIN_FOW_BIT_NORMAL) != 0;
			const bool bHasBlocker = BlockerLayerMask.IsValidIndex(Idx) && BlockerLayerMask[Idx] != 0;

			const FFixedPoint GroundZ = GroundHeight.IsValidIndex(Idx) ? GroundHeight[Idx] : Origin.Z;
			const FFixedPoint BlockZ  = bHasBlocker ? BlockerHeight[Idx] : GroundZ;
			const FFixedPoint TopZ    = (BlockZ > GroundZ) ? BlockZ : GroundZ;

			// Blocker cells track blocker-top; visible-over-blocker also rides
			// the blocker top so the cyan cell sits on the roof (matches what
			// a unit on that roof would see). Plain-ground cells stay at ground.
			FColor Color;
			FFixedPoint WZFP;
			if (bVisible)
			{
				Color = Cyan;
				WZFP = TopZ;
			}
			else if (bHasBlocker)
			{
				Color = Red;
				WZFP = BlockZ;
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
