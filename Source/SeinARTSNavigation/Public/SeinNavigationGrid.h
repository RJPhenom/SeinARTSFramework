/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationGrid.h
 * @brief   Deterministic grid-based navigation data for RTS pathfinding.
 *          Per-layer cell storage (DESIGN.md §13 "Multi-layer navigation"); flat
 *          maps have one layer. Per-map shared tag palette (up to 255 unique
 *          cell tags per map, u8 index). Tile partitioning for broadphase
 *          spatial queries / vision stamping / HPA* clusters.
 *          Fixed-point math for lockstep determinism.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "Grid/SeinCellAddress.h"
#include "Grid/SeinCellData.h"
#include "Grid/SeinNavigationLayer.h"
#include "Grid/SeinAbstractGraph.h"
#include "SeinNavigationGrid.generated.h"

/**
 * USeinNavigationGrid
 *
 * Canonical navigation data for a level. Holds one or more FSeinNavigationLayer
 * layers sharing XY extents + cell size. Layer 0 is ground; higher layers stack
 * upward for overpass/underpass / multi-story scenarios. Multi-layer transitions
 * happen via navlinks (Session 3.2), not via the grid directly.
 *
 * Owned at runtime by USeinNavigationSubsystem, populated either from a
 * USeinNavigationGridAsset (baked at edit time via ASeinNavVolume) or created
 * in-memory for tests.
 */
UCLASS(BlueprintType)
class SEINARTSNAVIGATION_API USeinNavigationGrid : public UObject
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------
	// Grid metadata (shared across all layers)
	// -------------------------------------------------------------------

	/** Grid width in cells (X axis). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	int32 GridWidth = 0;

	/** Grid height in cells (Y axis). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	int32 GridHeight = 0;

	/** World units per cell edge. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	FFixedPoint CellSize;

	/** World-space origin (bottom-left of layer 0). Layer baselines offset from here. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridOrigin;

	/** Tile size in cells (typically 32). Shared across layers. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Grid")
	int32 TileSize = 32;

	/** Per-map tag palette. Cells reference tags by u8 index into this array. Index 0 = null. */
	UPROPERTY()
	TArray<FGameplayTag> CellTagPalette;

	/** One layer per walkable plane. Flat maps have exactly one layer. */
	UPROPERTY()
	TArray<FSeinNavigationLayer> Layers;

	/** HPA*-style abstract graph spanning all layers + navlink edges (DESIGN §13). */
	UPROPERTY()
	FSeinAbstractGraph AbstractGraph;

	/** Per-navlink bake records (enable/disable, eligibility, edge indices). */
	UPROPERTY()
	TArray<FSeinNavLinkRecord> NavLinks;

	// -------------------------------------------------------------------
	// Initialization
	// -------------------------------------------------------------------

	/** Allocate a single-layer grid at the given extents, all cells walkable (cost 1). */
	void InitializeGrid(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin,
		ESeinElevationMode ElevationMode = ESeinElevationMode::None,
		int32 InTileSize = 32);

	/** Allocate a multi-layer grid. `LayerBaselinesZ` must match `NumLayers`. */
	void InitializeGridMultiLayer(int32 Width, int32 Height, FFixedPoint InCellSize, FFixedVector Origin,
		ESeinElevationMode ElevationMode,
		const TArray<float>& LayerBaselinesZ,
		int32 InTileSize = 32);

	/** Clear all layer data (keeps palette). Used by re-bake. */
	void ClearLayers();

	// -------------------------------------------------------------------
	// Coordinate conversions (layer-aware)
	// -------------------------------------------------------------------

	/** Convert a world-space XY to a (layer-0) grid cell coordinate. Legacy helper. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FIntPoint WorldToGrid(const FFixedVector& WorldPos) const;

	/** Convert a (layer-0) grid cell to its bottom-left world corner. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridToWorld(FIntPoint GridPos) const;

	/** Convert a (layer-0) grid cell to the world-space center of that cell. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FFixedVector GridToWorldCenter(FIntPoint GridPos) const;

	/** Resolve a world position to a full FSeinCellAddress (picks the layer whose
	 *  baseline Z is closest and ≤ WorldPos.Z). Returns Invalid if out of bounds. */
	FSeinCellAddress ResolveCellAddress(const FFixedVector& WorldPos) const;

	// -------------------------------------------------------------------
	// Queries (single-layer fast path — layer 0 defaults)
	// -------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	bool IsValidCell(FIntPoint GridPos) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	bool IsWalkable(FIntPoint GridPos) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	uint8 GetMovementCost(FIntPoint GridPos) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation|Grid")
	FGameplayTag GetTerrainType(FIntPoint GridPos) const;

	/** Full tag container for a cell (flat-path — 2 inline slots + overflow). */
	FGameplayTagContainer GetCellTags(FIntPoint GridPos, uint8 Layer = 0) const;

	/** Cell clearance (distance-transform output, 0 if clearance field unpopulated). */
	uint8 GetCellClearance(FIntPoint GridPos, uint8 Layer = 0) const;

	// -------------------------------------------------------------------
	// Layer-aware queries
	// -------------------------------------------------------------------

	FORCEINLINE int32 GetNumLayers() const { return Layers.Num(); }

	/** True if layer index + (X,Y) are in bounds. */
	bool IsValidCellAddr(const FSeinCellAddress& Addr) const;

	/** True if the addressed cell is walkable on its layer. */
	bool IsWalkableAddr(const FSeinCellAddress& Addr) const;

	/** Flat-index → (X,Y) for a given layer (caller supplies layer). */
	FIntPoint IndexToXY(int32 FlatIndex) const;

	// -------------------------------------------------------------------
	// Dynamic obstacles — runtime mutation
	// -------------------------------------------------------------------

	void MarkCellBlocked(FIntPoint GridPos, uint8 Layer = 0);
	void MarkCellUnblocked(FIntPoint GridPos, uint8 Layer = 0);
	void MarkAreaBlocked(FIntPoint Min, FIntPoint Max, uint8 Layer = 0);
	void MarkAreaUnblocked(FIntPoint Min, FIntPoint Max, uint8 Layer = 0);

	// -------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------

	/** Flat-index from (X,Y). Layer-0 default for legacy callers. */
	int32 CellIndex(FIntPoint GridPos) const;

	/** True if flat index is a valid cell on the given layer. */
	bool IsValidIndex(int32 Index, uint8 Layer = 0) const;

	/** 4- or 8-connected neighbors for a cell on the given layer. */
	TArray<FIntPoint> GetNeighbors(FIntPoint GridPos, bool bIncludeDiagonals = true) const;

	/** Resolve a tag to its palette index, adding it if not present (bake-time helper). */
	uint8 ResolveOrAddPaletteIndex(const FGameplayTag& Tag);

	/** Add a tag to a cell. Uses first free inline slot, or spills to overflow. */
	void AddCellTag(FIntPoint GridPos, const FGameplayTag& Tag, uint8 Layer = 0);

	/** Remove a tag from a cell (searches inline + overflow). */
	void RemoveCellTag(FIntPoint GridPos, const FGameplayTag& Tag, uint8 Layer = 0);

	// -------------------------------------------------------------------
	// Abstract graph + reachability (DESIGN §13)
	// -------------------------------------------------------------------

	/** Compute cluster ID for a cell on a layer (MVP: one cluster per tile). */
	int32 ClusterIDForCell(const FSeinCellAddress& Addr) const;

	/**
	 * Fast reachability query on the abstract graph. BFS from the source cluster
	 * node toward the goal cluster node, honoring per-edge `bEnabled` and
	 * (for navlinks) the agent's tag eligibility against EligibilityQuery.
	 *
	 * Used by path-reject plumbing (§13) to early-reject Move commands targeting
	 * cells that are pathing-unreachable before the ability activates.
	 */
	bool IsReachable(
		const FSeinCellAddress& From,
		const FSeinCellAddress& To,
		const FGameplayTagContainer& AgentTags) const;

	/** World-space variant — resolves cell addresses from positions internally. */
	bool IsLocationReachable(
		const FFixedVector& From,
		const FFixedVector& To,
		const FGameplayTagContainer& AgentTags) const;

	/** Toggle a navlink's enabled flag at runtime. Returns true if the link existed. */
	bool SetNavLinkEnabled(int32 NavLinkID, bool bEnabled);

private:
	/** Per-layer helper: validate bounds without constructing FSeinCellAddress. */
	const FSeinNavigationLayer* GetLayer(uint8 Layer) const;
	FSeinNavigationLayer* GetLayerMutable(uint8 Layer);
};
