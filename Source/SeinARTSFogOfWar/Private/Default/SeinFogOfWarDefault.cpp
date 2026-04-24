/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.cpp
 * @brief   MVP stamp engine. Flat-circle stamp, no LOS, no layers, no
 *          ownership, no refcount — recomputes the whole bitfield each tick.
 *          Shadowcast + lampshade + per-player + refcount land in the next
 *          passes; the surface contract (RegisterSource etc.) is stable so
 *          only the internals change.
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

DEFINE_LOG_CATEGORY_STATIC(LogSeinFogOfWarDefault, Log, All);

TSubclassOf<USeinFogOfWarAsset> USeinFogOfWarDefault::GetAssetClass() const
{
	return USeinFogOfWarDefaultAsset::StaticClass();
}

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
		CellBitfield.Reset();
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
	CellBitfield.SetNumZeroed(NumCells);

	// Dequantize baked uint8 GroundHeight → FFixedPoint.
	// quantized * HeightQuantum + MinHeight = world Z.
	const FFixedPoint MinH = Asset->MinHeight;
	const FFixedPoint Quantum = Asset->HeightQuantum;
	for (int32 Idx = 0; Idx < NumCells; ++Idx)
	{
		const FSeinFogOfWarCell& Cell = Asset->Cells[Idx];
		GroundHeight[Idx] = MinH + Quantum * FFixedPoint::FromInt(Cell.GroundHeight);
	}
}

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
	CellBitfield.SetNumZeroed(NumCells);

	// Per-cell downward trace for terrain Z. Capped at InitTraceCellCap; beyond
	// that snap to the volume mid-Z to avoid a multi-second init hitch on huge
	// maps. (Bake pipeline replaces this with pre-computed quantized heights
	// in USeinFogOfWarDefaultAsset.)
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
				if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
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

void USeinFogOfWarDefault::TickStamps(UWorld* World)
{
	if (Width <= 0 || Height <= 0 || !World) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinVisionData::StaticStruct());
	if (!Storage) return;

	// Clear visibility bits but preserve the Explored bit (it's sticky —
	// once a cell is seen, history stays for the match).
	for (uint8& Byte : CellBitfield)
	{
		Byte &= SEIN_FOW_BIT_EXPLORED;
	}

	// Walk every live entity. If it carries FSeinVisionData, stamp its
	// vision circle into the bitfield.
	Sim->GetEntityPool().ForEachEntity(
		[this, Storage](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinVisionData* VData = static_cast<const FSeinVisionData*>(Raw);
			if (!VData) return;
			if (VData->VisionRadius <= FFixedPoint::Zero) return;

			StampFlatCircle(Entity.Transform.GetLocation(), VData->VisionRadius);
		});

	OnFogOfWarMutated.Broadcast();
}

void USeinFogOfWarDefault::StampFlatCircle(const FFixedVector& WorldPos, FFixedPoint Radius)
{
	int32 CX, CY;
	if (!WorldToGrid(WorldPos, CX, CY)) return;

	// TODO(det): radius math in FFixedPoint for replay determinism. MVP uses
	// float for readability — acceptable while stamp output isn't replicated
	// sim state yet (debug viz + LOS checks feed off it, both tolerant of a
	// float rounding delta).
	const float CellSizeF = CellSize.ToFloat();
	if (CellSizeF <= 0.0f) return;
	const float RadiusCellsF = Radius.ToFloat() / CellSizeF;
	const int32 RadiusCells = FMath::CeilToInt(RadiusCellsF);
	const float RadiusSqF = RadiusCellsF * RadiusCellsF;

	const uint8 StampMask = SEIN_FOW_BIT_NORMAL | SEIN_FOW_BIT_EXPLORED;

	for (int32 DY = -RadiusCells; DY <= RadiusCells; ++DY)
	{
		const int32 Y = CY + DY;
		if (Y < 0 || Y >= Height) continue;
		const int32 DYSq = DY * DY;
		for (int32 DX = -RadiusCells; DX <= RadiusCells; ++DX)
		{
			const float DistSq = static_cast<float>(DX * DX + DYSq);
			if (DistSq > RadiusSqF) continue;
			const int32 X = CX + DX;
			if (X < 0 || X >= Width) continue;
			CellBitfield[CellIndex(X, Y)] |= StampMask;
		}
	}
}

uint8 USeinFogOfWarDefault::GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const
{
	int32 CX, CY;
	if (!WorldToGrid(WorldPos, CX, CY)) return 0;
	return CellBitfield[CellIndex(CX, CY)];
}

void USeinFogOfWarDefault::CollectDebugCellQuads(FSeinPlayerID Observer,
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
	if (Width <= 0 || Height <= 0) return;

	OutHalfExtent = CellSize.ToFloat() * 0.5f;
	const int32 NumCells = Width * Height;
	OutCenters.Reserve(NumCells);
	OutColors.Reserve(NumCells);

	const float OriginXF = Origin.X.ToFloat();
	const float OriginYF = Origin.Y.ToFloat();
	const float CellSizeF = CellSize.ToFloat();
	const float HalfCellF = CellSizeF * 0.5f;

	// Cyan = currently visible (V bit); red = in-grid but not visible. Debug
	// viz: Explored-but-not-visible intentionally collapses to the plain red
	// channel for MVP — once ownership + per-player VisionGroups land, the
	// dim-red "fog of war memory" color will reintroduce.
	const FColor Cyan(0, 200, 255);
	const FColor Red (200, 0, 0);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 Bits = CellBitfield[Idx];
			const bool bVisible = (Bits & SEIN_FOW_BIT_NORMAL) != 0;

			const float WX = OriginXF + CellSizeF * X + HalfCellF;
			const float WY = OriginYF + CellSizeF * Y + HalfCellF;
			const float WZ = GroundHeight.IsValidIndex(Idx) ? GroundHeight[Idx].ToFloat() : Origin.Z.ToFloat();

			// Sit well above nav's debug layer (+2cm) and clear of z-buffer
			// precision fuzz at camera distance. See SEIN_FOW_DEBUG_Z_OFFSET.
			OutCenters.Emplace(WX, WY, WZ + SEIN_FOW_DEBUG_Z_OFFSET);
			OutColors.Add(bVisible ? Cyan : Red);
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
