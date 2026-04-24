/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStar.h
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
#include "SeinNavigationAStar.generated.h"

class UWorld;
class USeinNavigationAStarAsset;

UCLASS(BlueprintType, meta = (DisplayName = "Sein Navigation (A*)"))
class SEINARTSNAVIGATION_API USeinNavigationAStar : public USeinNavigation
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
	 *  scene proxy. Gated by `ShowFlags.Navigation` / `SeinARTS.Debug.ShowNavigation`. */
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

	/** Precomputed tan²(MaxWalkableSlopeDegrees). Used only at bake time when
	 *  computing cell connectivity — the A* pathing reads CellConnections bits
	 *  rather than re-checking slope live. */
	FFixedPoint MaxSlopeTanSq = FFixedPoint::One;

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
	bool DoSyncBake(UWorld* World, USeinNavigationAStarAsset*& OutAsset);
	void ApplyAssetData(const USeinNavigationAStarAsset* Asset);

#if WITH_EDITOR
	USeinNavigationAStarAsset* CreateOrLoadAsset(UWorld* World, const FString& AssetName) const;
	bool SaveAssetToDisk(USeinNavigationAStarAsset* Asset) const;
#endif
};
