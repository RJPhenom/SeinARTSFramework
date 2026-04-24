/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.h
 * @brief   Reference implementation of USeinFogOfWar. MVP scope: single
 *          global bitfield (no per-player ownership), V-bit only (no custom
 *          layers), flat-circle stamps (no shadowcast/LOS), per-tick full
 *          recompute (no refcount / stamp-delta). Grid auto-sizes from
 *          ASeinFogOfWarVolumes on OnWorldBeginPlay when no bake is loaded
 *          so designers get stamping + debug viz before the full bake +
 *          shadowcast pipeline lands.
 *
 *          Upgrades in later passes:
 *          - Shadowcast + lampshade eye-height blocker test (CoH TrueSight)
 *          - Per-player VisionGroup grids + ownership filter
 *          - 6 custom layer bits (Stealth, Thermal, etc.)
 *          - Stamp-delta refcount (only recompute on cell-cross / prop change)
 *          - Fixed-point radius math for determinism
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogOfWar.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinFogOfWarDefault.generated.h"

class UWorld;
class USeinFogOfWarDefaultAsset;

UCLASS(BlueprintType, meta = (DisplayName = "Sein Fog Of War (Default)"))
class SEINARTSFOGOFWAR_API USeinFogOfWarDefault : public USeinFogOfWar
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Designer config
	// ----------------------------------------------------------------------

	/** Cap cell count for the per-cell terrain trace at init. Beyond this,
	 *  all cells snap to the volume's mid-Z (avoids multi-second editor hitch
	 *  on huge maps). Dev-viz-only quality gate. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0"))
	int32 InitTraceCellCap = 20000;

	// ----------------------------------------------------------------------
	// USeinFogOfWar overrides
	// ----------------------------------------------------------------------

	virtual TSubclassOf<USeinFogOfWarAsset> GetAssetClass() const override;

	virtual void LoadFromAsset(USeinFogOfWarAsset* Asset) override;
	virtual bool HasRuntimeData() const override { return Width > 0 && Height > 0; }

	virtual void InitGridFromVolumes(UWorld* World) override;
	virtual void TickStamps(UWorld* World) override;

	virtual uint8 GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const override;

	virtual void CollectDebugCellQuads(FSeinPlayerID Observer,
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

private:

	// ----------------------------------------------------------------------
	// Runtime grid
	// ----------------------------------------------------------------------
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSize = FFixedPoint::FromInt(400);
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Per-cell terrain Z (from bake or downward traces at init). Row-major. */
	TArray<FFixedPoint> GroundHeight;

	/** EVNNNNNN per cell (row-major). MVP uses only bit 0 (Explored, sticky)
	 *  and bit 1 (V / Normal visibility). Bits 2..7 reserved for future
	 *  custom-layer support. */
	TArray<uint8> CellBitfield;

	// ----------------------------------------------------------------------
	// Helpers
	// ----------------------------------------------------------------------
	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;

	/** OR the V + Explored bits into every cell within `Radius` world units
	 *  of `WorldPos`. Flat circle — no LOS, no lampshade, no layers. */
	void StampFlatCircle(const FFixedVector& WorldPos, FFixedPoint Radius);

	/** Hoist baked asset fields into runtime arrays. Called from LoadFromAsset
	 *  when the asset is a USeinFogOfWarDefaultAsset. */
	void ApplyAssetData(const USeinFogOfWarDefaultAsset* Asset);
};
