/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.h
 * @brief   Reference implementation of USeinFogOfWar. Grid loads from a
 *          USeinFogOfWarDefaultAsset (bake output) when available; auto-sizes
 *          from ASeinFogOfWarVolumes when not. MVP stamp is flat-circle;
 *          shadowcast + lampshade + per-player + custom layers land in
 *          subsequent passes.
 *
 *          Determinism contract:
 *          - Stamp output (CellBitfield): fully FFixedPoint. StampFlatCircle
 *            uses FFixedPoint radius math + cell² integer comparisons; no
 *            float on the stamp path.
 *          - Stamp cadence: driven by USeinWorldSubsystem::OnSimTickCompleted
 *            gated by plugin-settings VisionTickInterval. All clients stamp
 *            against the same tick-N source snapshot — lockstep-safe.
 *          - LOS query: FSeinLineOfSightResolver takes FFixedVector; no
 *            float round-trip between sim caller and impl cell lookup.
 *          - Grid layout (Width/Height/Origin/CellSize): derived from AVolume
 *            bounds via FromFloat. Deterministic on IEEE-754 targets with
 *            stock UE floating-point modes (the platforms UE5 ships for).
 *          - Bake is editor-only. Produces quantized per-cell Ground +
 *            Blocker heights stored in USeinFogOfWarDefaultAsset; runtime
 *            dequantizes into FFixedPoint arrays on load.
 *          - Debug viz (CollectDebugCellQuads): float conversions at the
 *            FFixedPoint → FVector boundary, render-only, not sim state.
 *
 *          Upgrades in later passes:
 *          - Shadowcast + lampshade eye-height blocker test (CoH TrueSight)
 *          - Per-player VisionGroup grids + ownership filter
 *          - 6 custom layer bits (Stealth, Thermal, etc.)
 *          - Stamp-delta refcount (only recompute on cell-cross / prop change)
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogOfWar.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinFogOfWarDefault.generated.h"

class UWorld;
class USeinFogOfWarDefaultAsset;

/**
 * Per-observer visibility state. One per FSeinPlayerID; lazily created on
 * the owner's first stamp. Holds the EVNNNNNN bitfield the player sees —
 * V bits clear per tick, Explored (bit 0) is sticky for the match.
 */
struct FSeinFogVisionGroup
{
	TArray<uint8> CellBitfield;
};

UCLASS(BlueprintType, meta = (DisplayName = "Sein Fog Of War (Default)"))
class SEINARTSFOGOFWAR_API USeinFogOfWarDefault : public USeinFogOfWar
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Designer config
	// ----------------------------------------------------------------------

	/** Vertical extent (world units) above the tallest fog volume that bake
	 *  traces start from. Bump if any sight-blocker geometry sits near the
	 *  top of your volumes. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float BakeTraceHeadroom = 400.0f;

	/** Minimum gap (world units) between the top trace hit and the ground
	 *  trace hit below it for the difference to register as a static
	 *  blocker. Filters mesh-thickness noise (e.g. terrain with a few-cm-
	 *  thick top surface shouldn't read as a 3cm blocker). */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float StaticBlockerMinHeight = 15.0f;

	/** Collision channel bake traces sample for ground + blocker detection.
	 *  ECC_Visibility is the correct default — walls / buildings / terrain
	 *  respond to it by default. Teams with a custom "SightBlocker" channel
	 *  can override here. */
	UPROPERTY(EditAnywhere, Category = "Bake")
	TEnumAsByte<ECollisionChannel> BakeTraceChannel = ECC_Visibility;

	/** Cap cell count for the per-cell terrain trace at init. Beyond this,
	 *  cells snap to volume mid-Z (avoids multi-second editor hitch on huge
	 *  maps when no bake is loaded). Bake itself has no cap — it's an
	 *  explicit designer action with progress UI. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0"))
	int32 InitTraceCellCap = 20000;

	// ----------------------------------------------------------------------
	// USeinFogOfWar overrides
	// ----------------------------------------------------------------------

	virtual TSubclassOf<USeinFogOfWarAsset> GetAssetClass() const override;

	virtual bool BeginBake(UWorld* World) override;
	virtual bool IsBaking() const override { return bBaking; }
	virtual void RequestCancelBake() override { bCancelRequested = true; }

	virtual void LoadFromAsset(USeinFogOfWarAsset* Asset) override;
	virtual bool HasRuntimeData() const override { return Width > 0 && Height > 0; }

	virtual void InitGridFromVolumes(UWorld* World) override;
	virtual void TickStamps(UWorld* World) override;

	virtual uint8 GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const override;

	virtual void CollectDebugCellQuads(FSeinPlayerID Observer,
		int32 VisibleBitIndex,
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

	/** Per-cell static blocker height (absolute world Z of the top of the
	 *  blocker; zero = no static blocker at this cell). Populated by bake;
	 *  shadowcast step consumes it + a parallel dynamic overlay grid. */
	TArray<FFixedPoint> BlockerHeight;

	/** Per-cell static blocker layer mask — bits 1..7 indicate which of
	 *  the EVNNNNNN vis layers this blocker occludes. MVP bake sets all
	 *  vis bits (0xFE) for every detected blocker; per-layer masks
	 *  (e.g. thermal-see-through-smoke) land with the layer pass. */
	TArray<uint8> BlockerLayerMask;

	/** Dynamic blocker overlay — absolute world Z of any runtime-authored
	 *  blocker (smoke grenades, destructibles in progress) at this cell.
	 *  Rebuilt each `TickStamps` from entities carrying `FSeinVisionBlockerData`
	 *  in sim component storage. Zero = no dynamic blocker this tick.
	 *  LOS tests static + dynamic independently (per-blocker layer mask
	 *  is honored separately — see IsCellOpaqueToEye). */
	TArray<FFixedPoint> DynamicBlockerHeight;

	/** Parallel to `DynamicBlockerHeight` — which EVNNNNNN bits the
	 *  dynamic blocker at this cell occludes. OR'd across overlapping
	 *  blockers (e.g. two smoke grenades at the same cell combine their
	 *  masks). */
	TArray<uint8> DynamicBlockerLayerMask;

	/** Per-observer visibility state. Keyed by FSeinPlayerID; lazily
	 *  created on first stamp by that owner. Each group's CellBitfield is
	 *  sized Width*Height; bit 0 is sticky Explored, bit 1 is Normal vis
	 *  (cleared + re-stamped per tick), bits 2..7 reserved for custom
	 *  layers. Neutral (player ID 0) is a legal key — useful for
	 *  neutral-owned structures with cheap passive vision. */
	TMap<FSeinPlayerID, FSeinFogVisionGroup> VisionGroups;

	// ----------------------------------------------------------------------
	// Bake state
	// ----------------------------------------------------------------------
	bool bBaking = false;
	bool bCancelRequested = false;

	// ----------------------------------------------------------------------
	// Helpers
	// ----------------------------------------------------------------------
	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;

	/** Get-or-create the VisionGroup for a player. Lazy-inits the bitfield
	 *  to Width*Height when first observed; reuses across ticks (V bits
	 *  clear, Explored stays sticky). */
	FSeinFogVisionGroup& GetOrCreateGroup(FSeinPlayerID PlayerID);

	/** Stamp `StampBit` + Explored bits into every cell within `Radius`
	 *  world units of `WorldPos` that the source can actually see —
	 *  writing into `OwnerPlayer`'s VisionGroup. `StampBit` ∈ [1, 7]:
	 *  1 = V (Normal), 2..7 = N0..N5 custom layers. Lampshade model:
	 *  source eye Z = `WorldPos.Z + EyeHeight` (unit's actual sim Z,
	 *  tracked by nav path — NOT the cell's baked GroundHeight, which
	 *  stores the terrain beneath any blocker and would place a unit
	 *  standing on a climbable platform below its walls). Terrain
	 *  (GroundHeight) occludes layer-agnostically; static blockers only
	 *  occlude if their `BlockerLayerMask` covers this layer's bit —
	 *  which means, for example, smoke tagged to block Normal but not
	 *  Thermal will let a Thermal stamp pass through. Per-target LOS uses
	 *  integer Bresenham — O(R³) per source but symmetric, deterministic,
	 *  and trivial to verify. Swap to Ford's symmetric shadowcast (O(R²))
	 *  when radii grow large. */
	void StampShadowcast(FSeinPlayerID OwnerPlayer, const FFixedVector& WorldPos,
		FFixedPoint Radius, FFixedPoint EyeHeight, uint8 StampBit);

	/** Integer-Bresenham LOS from (X0,Y0,EyeZ) to (X1,Y1,TargetZ). At each
	 *  intermediate cell the ray's Z is linearly interpolated between
	 *  EyeZ and TargetZ; the cell is tested against that interpolated Z
	 *  (not the constant EyeZ). This is what gives true-sight its
	 *  elevation behavior: a unit on a roof looking DOWN at the ground
	 *  has its ray descend, so a wall halfway between them — shorter than
	 *  the unit's eye but taller than the ray's Z at that midpoint — will
	 *  still occlude. Only blockers whose LayerMask covers `StampBitMask`
	 *  contribute. */
	bool HasLineOfSightToCell(int32 X0, int32 Y0, int32 X1, int32 Y1,
		FFixedPoint EyeZ, FFixedPoint TargetZ, uint8 StampBitMask) const;

	/** Cell is opaque to an eye at EyeZ iff any of:
	 *   - GroundHeight > EyeZ (terrain always occludes), OR
	 *   - Static BlockerHeight > EyeZ AND BlockerLayerMask & StampBitMask, OR
	 *   - Dynamic BlockerHeight > EyeZ AND DynamicBlockerLayerMask & StampBitMask.
	 *  Static + dynamic test independently — a short-but-layered dynamic
	 *  blocker doesn't inherit a tall static blocker's reach, and vice
	 *  versa. Lets smoke-blocks-Normal-but-not-Thermal style policies work
	 *  correctly alongside baked geometry. */
	bool IsCellOpaqueToEye(int32 X, int32 Y, FFixedPoint EyeZ, uint8 StampBitMask) const;

	/** Clear the dynamic blocker overlay, then walk every entity carrying
	 *  `FSeinVisionBlockerData` and disc-stamp its contribution into the
	 *  overlay. Runs at the top of TickStamps so the vision passes below
	 *  see the freshest occlusion state. */
	void RebuildDynamicBlockers(UWorld* World);

	/** Stamp a disc of effective height + layer mask into the dynamic
	 *  blocker overlay. `Height` is blocker height above ground; at each
	 *  covered cell the stored value is `GroundHeight[Idx] + Height`
	 *  (absolute world Z of the blocker top, matching the static array
	 *  convention). Multiple overlapping disc stamps take the taller
	 *  height + OR'd layer mask per cell. */
	void StampDynamicBlockerDisc(const FFixedVector& WorldPos,
		FFixedPoint Radius, FFixedPoint HeightAboveGround, uint8 LayerMask);

	/** Hoist baked asset fields into runtime arrays. Called from LoadFromAsset
	 *  when the asset is a USeinFogOfWarDefaultAsset. */
	void ApplyAssetData(const USeinFogOfWarDefaultAsset* Asset);

	/** Synchronous bake. Walks volumes, double-traces each cell for ground +
	 *  blocker heights, quantizes into the asset, saves to disk (editor).
	 *  Returns false on cancel / no volumes / save failure. */
	bool DoSyncBake(UWorld* World, USeinFogOfWarDefaultAsset*& OutAsset);

#if WITH_EDITOR
	USeinFogOfWarDefaultAsset* CreateOrLoadAsset(UWorld* World, const FString& AssetName) const;
	bool SaveAssetToDisk(USeinFogOfWarDefaultAsset* Asset) const;
#endif
};
