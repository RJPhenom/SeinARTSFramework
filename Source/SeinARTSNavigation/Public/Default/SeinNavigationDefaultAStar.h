/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationDefaultAStar.h
 * @brief   Reference implementation of USeinNavigation — single-layer 2D grid
 *          baked from downward line-traces, with synchronous A* pathfinding and
 *          line-of-sight path smoothing. Minimal on purpose; serves as the
 *          default nav plus an example for custom subclasses.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNavigation.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationDefaultAStar.generated.h"

class UWorld;
class USeinNavigationDefaultAStarAsset;

UCLASS(BlueprintType, meta = (DisplayName = "Sein Nav (A*)"))
class SEINARTSNAVIGATION_API USeinNavigationDefaultAStar : public USeinNavigation
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Designer config (edit on the nav CDO via class defaults — class is
	// instantiated from plugin settings, so these values apply per-project.)
	// ----------------------------------------------------------------------

	/** Maximum walkable slope angle in degrees. Surfaces steeper than this are
	 *  treated as blocked at bake time. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxWalkableSlopeDegrees = 45.0f;

	/** Vertical extent (world units) above the tallest NavVolume that the bake
	 *  traces start from. Bump this if your walkable surfaces sit near the top
	 *  of the volume and you want a margin. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float BakeTraceHeadroom = 200.0f;

	/** Emit cell quads (green = walkable, red = blocked) for the nav debug
	 *  scene proxy. Gated by `ShowFlags.Navigation` / `Sein.Nav.Show`. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawCellsInDebug = true;

	// ----------------------------------------------------------------------
	// USeinNavigation overrides
	// ----------------------------------------------------------------------

	virtual TSubclassOf<USeinNavigationAsset> GetAssetClass() const override;
	virtual bool BeginBake(UWorld* World) override;
	virtual bool IsBaking() const override { return bBaking; }
	virtual void RequestCancelBake() override { bCancelRequested = true; }

	virtual void LoadFromAsset(USeinNavigationAsset* Asset) override;
	virtual bool HasRuntimeData() const override { return CellCost.Num() > 0; }

	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;
	virtual bool IsPassable(const FFixedVector& WorldPos) const override;
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const override;
	virtual bool GetGroundHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;
	virtual void SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& InBlockers) override;

	// Debug collectors — declarations stay in all build configs (ABI); bodies
	// are compiled out in shipping via UE_ENABLE_DEBUG_DRAWING in the .cpp.
	virtual void CollectDebugCellQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const override;
	virtual void CollectDebugPathCells(
		const FFixedVector& AgentPos,
		const TArray<FFixedVector>& Waypoints,
		int32 CurrentWaypointIndex,
		TArray<FVector>& OutRemainingCells,
		TArray<FVector>& OutCurrentTargetCell,
		float& OutHalfExtent) const override;
	virtual void CollectDebugBlockerCells(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

private:

	// ----------------------------------------------------------------------
	// Runtime grid (populated by LoadFromAsset / bake)
	// ----------------------------------------------------------------------
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSize = FFixedPoint::FromInt(100);
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Per-cell cost. 0 = blocked, 1..254 = passable with cost multiplier, 255 = impassable. */
	TArray<uint8> CellCost;

	/** Per-cell center-height (world-space Z) — snapped-to placement for units. */
	TArray<FFixedPoint> CellHeight;

	/** Per-cell 8-direction connectivity bitmask (baked). Bit N is set iff a
	 *  unit can traverse from this cell to its neighbor at direction index N.
	 *  Queried directly by A* + path smoother — no live slope math at query
	 *  time, so the rules applied at bake are guaranteed to match the rules
	 *  enforced at runtime. */
	TArray<uint8> CellConnections;

	/** Runtime list of dynamic blockers, refreshed each PreTick by the
	 *  nav-blocker stamping system. FindPath rebuilds the per-call
	 *  DynamicBlocked overlay from this list (excluding the requester so
	 *  a unit can path out of its own footprint). */
	TArray<FSeinDynamicBlocker> DynamicBlockers;

	/** Per-cell flag (1 = dynamically blocked for this FindPath, 0 = clear).
	 *  Mutable so it can be rebuilt inside the const FindPath. Single-threaded
	 *  sim guarantees no concurrent FindPath; the buffer is reused across
	 *  calls to avoid per-call allocations. */
	mutable TArray<uint8> DynamicBlocked;

	/** Fingerprint of the last DynamicBlockers list pushed via SetDynamicBlockers.
	 *  XOR-fold of per-blocker pose + mask + shape hash. SetDynamicBlockers
	 *  only broadcasts OnNavigationMutated when the new hash differs — gates
	 *  the debug scene-proxy rebuild to actual mutations instead of every
	 *  per-tick push. */
	uint32 LastBlockerHash = 0;

	// ----------------------------------------------------------------------
	// Bake state
	// ----------------------------------------------------------------------
	bool bBaking = false;
	bool bCancelRequested = false;

	// ----------------------------------------------------------------------
	// Grid helpers
	// ----------------------------------------------------------------------
	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
	FORCEINLINE bool IsCellPassable(int32 X, int32 Y) const
	{
		if (!IsValidCoord(X, Y)) return false;
		const uint8 C = CellCost[CellIndex(X, Y)];
		return C > 0 && C < 255;
	}

	/** Effective passability for a FindPath in progress: static cell + the
	 *  current request's dynamic-blocked overlay. DynamicBlocked is rebuilt
	 *  at the top of FindPath (with the requester excluded + agent mask
	 *  applied), so callers inside that scope (A*, HasLineOfSight) can use
	 *  this directly. */
	FORCEINLINE bool IsCellPassableForPath(int32 X, int32 Y) const
	{
		if (!IsCellPassable(X, Y)) return false;
		if (DynamicBlocked.Num() == 0) return true;
		return DynamicBlocked[CellIndex(X, Y)] == 0;
	}

	/** Stamp DynamicBlockers (skipping `Exclude` + filtering to those whose
	 *  BlockedNavLayerMask intersects `AgentNavLayerMask`) into DynamicBlocked.
	 *  Called at the top of FindPath so the overlay matches BOTH the
	 *  requester (self-exclusion) AND the agent's layer (water blocker
	 *  doesn't stamp for amphibious agents). */
	void BuildDynamicBlockedOverlay(FSeinEntityHandle Exclude, uint8 AgentNavLayerMask) const;

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;
	FFixedVector GridToWorld(int32 X, int32 Y) const;

	/** Bresenham grid line-of-sight — true if every cell from (X0,Y0) to (X1,Y1)
	 *  is passable. Used for path smoothing. */
	bool HasLineOfSight(int32 X0, int32 Y0, int32 X1, int32 Y1) const;

	/** Flood-fill-smoothed path from A* cell chain to world-space waypoints. */
	void BuildSmoothedPath(const TArray<FIntPoint>& CellPath, FSeinPath& OutPath) const;

	/** Core A* search. Returns empty array if no path found. */
	TArray<FIntPoint> AStarSearch(FIntPoint Start, FIntPoint End) const;

	// ----------------------------------------------------------------------
	// Bake pipeline (synchronous, editor-blocking slow-task progress)
	// ----------------------------------------------------------------------
	bool DoSyncBake(UWorld* World, USeinNavigationDefaultAStarAsset*& OutAsset);
	void ApplyAssetData(const USeinNavigationDefaultAStarAsset* Asset);

#if WITH_EDITOR
	USeinNavigationDefaultAStarAsset* CreateOrLoadAsset(UWorld* World, const FString& AssetName) const;
	bool SaveAssetToDisk(USeinNavigationDefaultAStarAsset* Asset) const;
#endif
};
