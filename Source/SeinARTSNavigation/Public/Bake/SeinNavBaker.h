/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavBaker.h
 * @brief   Tick-driven bake pipeline for SeinARTS nav data (DESIGN.md §13
 *          "Async bake + editor notifications"). Runs on the game thread via
 *          FTSTicker to keep collision traces thread-safe while remaining
 *          non-blocking for the editor UI. Progress reported through an
 *          SNotificationItem toast with a % bar + cancel button.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Ticker.h"
#include "Math/Box.h"
#include "Grid/SeinCellData.h"
#include "Grid/SeinNavigationLayer.h"
#include "SeinNavBaker.generated.h"

class UWorld;
class ASeinNavVolume;
class USeinNavigationGridAsset;

/** Snapshot of resolved bake parameters (computed once at kickoff). */
struct FSeinNavBakeParams
{
	FBox UnionWorldBounds = FBox(ForceInit);
	float CellSize = 100.0f;
	ESeinElevationMode ElevationMode = ESeinElevationMode::None;
	float LayerSeparation = 100.0f;
	int32 TileSize = 32;
	float MaxWalkableSlopeCos = 0.707f; // cos(45°) default

	int32 GridWidth = 0;
	int32 GridHeight = 0;
	FVector GridOriginWorld = FVector::ZeroVector;
	FVector TopOfBake = FVector::ZeroVector; // high Z used as trace start
	float TotalHeight = 0.0f;

	/** Multi-layer baking is enabled whenever the map cares about Z at all —
	 *  i.e., any elevation mode other than None. Flat maps produce exactly one layer
	 *  because a LineTraceMulti column only returns one up-facing hit. */
	bool IsMultiLayer() const { return ElevationMode != ESeinElevationMode::None; }

	/** Snapshot of agent radius classes (world-unit radii). Empty = single clearance field. */
	TArray<float> AgentRadiusClasses;
};

/**
 * USeinNavBaker — drives an editor-time nav bake across ticks.
 *
 * Lifecycle:
 *   1. Editor code calls `USeinNavBaker::BeginBake(World)` which:
 *      - Collects all ASeinNavVolume actors; unions bounds; resolves params.
 *      - Allocates a transient USeinNavigationGridAsset.
 *      - Creates a progress notification (SNotificationItem) with a cancel button.
 *      - Registers a ticker that advances bake state each frame.
 *   2. Each tick processes `Settings->BakeTilesPerProgressStep` tiles:
 *      - For each cell in the tile, trace downward from TopOfBake using
 *        `FCollisionQueryParams::bFindInitialOverlaps = false` + a channel that
 *        respects `UPrimitiveComponent::CanEverAffectNavigation`.
 *      - Record walkability, height, modifier overrides.
 *      - Multi-layer: continue tracing below the first hit to populate stacked layers.
 *   3. On all tiles processed:
 *      - Run distance-transform pass producing per-cell Clearance.
 *      - Save the grid asset, update NavVolumes to point at it, transition toast to Success.
 *   4. Cancel path sets toast to None and throws away the in-progress asset.
 *
 * This class lives in the runtime module so a non-editor game could still
 * trigger a bake (e.g., user-generated map editor). Editor UI wiring lives in
 * SeinARTSEditor / SeinNavVolumeDetails customization.
 */
UCLASS()
class SEINARTSNAVIGATION_API USeinNavBaker : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Start a bake covering every ASeinNavVolume in `World`. Reentrant-safe:
	 * if a bake is already running, returns nullptr and does nothing.
	 * @return The live baker instance (rooted for the duration of the bake) or nullptr.
	 */
	static USeinNavBaker* BeginBake(UWorld* World);

	/** True while a bake is in progress. */
	static bool IsBaking();

	/** Request cancellation. The next tick will tear down + toast "Cancelled". */
	void RequestCancel();

	/** Baked asset (valid once completed, nullptr while in-progress or on failure). */
	USeinNavigationGridAsset* GetBakedAsset() const { return BakedAsset; }

private:
	bool TickBake(float DeltaTime);

	void ProcessTile(int32 TileIndex);
	void TraceCell(int32 WorldX, int32 WorldY);

	/** O(N) two-pass Chebyshev-approximate distance transform. */
	void ComputeClearanceForLayer(int32 LayerIdx);

	/** Build HPA* cluster nodes + cluster-border edges for one layer. */
	void BuildAbstractGraphForLayer(int32 LayerIdx);

	/** Collect ASeinNavLinkProxy actors in the baker's world + add them as abstract-graph edges. */
	void IntegrateNavLinks();

	/** Write the baked asset to disk, point all volumes at it. */
	void Finalize();

	/** Toast helpers (no-ops in non-editor builds). */
	void ShowToast(const FText& Message);
	void UpdateToast(float Fraction, const FText& StatusLine);
	void DismissToast(bool bSuccess, const FText& FinalText);

private:
	UPROPERTY()
	TWeakObjectPtr<UWorld> WorldPtr;

	UPROPERTY()
	TArray<TWeakObjectPtr<ASeinNavVolume>> Volumes;

	UPROPERTY()
	TObjectPtr<USeinNavigationGridAsset> BakedAsset;

	FSeinNavBakeParams Params;
	FTSTicker::FDelegateHandle TickHandle;

	int32 NextTileIndex = 0;
	int32 TotalTiles = 0;
	int32 TilesPerStep = 16;

	/** Per-layer hit-Z accumulator (TraceCell appends; finalize averages into LayerBaselineZ
	 *  for Flat elevation mode so the debug viz + flow fields read the actual floor Z, not
	 *  the volume's Min.Z). Parallel to BakedAsset->Layers. */
	TArray<double> LayerHitZSum;
	TArray<int32> LayerHitZCount;

	bool bCancelRequested = false;
	bool bComputingClearance = false;
	int32 ClearanceLayerCursor = 0;
	bool bBuildingAbstractGraph = false;
	int32 GraphLayerCursor = 0;
	bool bIntegratingNavLinks = false;

	/** Diagnostic: per-cell hit-count histogram. Tells us whether multi-layer traces
	 *  are even finding stacked geometry. If CellsWithMultiHits stays at 0 while the
	 *  user has an overpass, the volume probably doesn't span both floors, or the
	 *  overpass geometry isn't nav-relevant. Logged in Finalize. */
	int32 CellsWithZeroHits = 0;
	int32 CellsWithOneHit = 0;
	int32 CellsWithMultiHits = 0;
	int32 MaxHitsOnAnyCell = 0;

#if WITH_EDITOR
	TSharedPtr<class SNotificationItem> NotificationItem;
#endif

	static TWeakObjectPtr<USeinNavBaker> ActiveBaker;
};
