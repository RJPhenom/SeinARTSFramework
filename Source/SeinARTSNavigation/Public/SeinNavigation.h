/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.h
 * @brief   Abstract base class for pluggable navigation implementations.
 *
 *          USeinNavigation owns the end-to-end nav problem for one world:
 *          bake pipeline, baked-asset storage, runtime pathfinding, reachability,
 *          and (optionally) debug visualization. It is the ONLY thing the
 *          framework's MoveTo action, editor bake button, and ability validation
 *          delegate talk to.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::NavigationClass`).
 *          The framework ships `USeinNavigationAStar` as a minimal 2D-grid +
 *          A* reference implementation; game teams can subclass or replace it
 *          entirely with navmesh-, waypoint-, or hierarchical-graph-based impls
 *          without touching any other framework code.
 *
 *          Decoupling contract:
 *          - The MoveTo action, BPFL, volume actor, and ability validation
 *            delegate call into this class's virtual surface only.
 *          - Concrete subclasses own their own data storage + bake strategy.
 *          - Baked data is stored in a USeinNavigationAsset subclass (impl-
 *            specific); ASeinNavVolume holds a polymorphic reference.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.h"
#include "SeinNavigation.generated.h"

class UWorld;
class ASeinNavVolume;
class USeinNavigationAsset;

/** Fired when the nav's baked data mutates (bake finished, asset re-loaded,
 *  dynamic obstacle change). Cached plans must re-query on this signal. */
DECLARE_MULTICAST_DELEGATE(FSeinOnNavigationMutated);

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Navigation"))
class SEINARTSNAVIGATION_API USeinNavigation : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinNavigationSubsystem
	// ----------------------------------------------------------------------

	/** Called once when the subsystem instantiates this nav. Default: no-op. */
	virtual void OnNavigationInitialized(UWorld* World) {}

	/** Called once when the subsystem is tearing down this nav. Default: no-op. */
	virtual void OnNavigationDeinitialized() {}

	// ----------------------------------------------------------------------
	// Bake (editor / dev-loop)
	// ----------------------------------------------------------------------

	/** The UDataAsset class this nav produces when baking. Subclasses return
	 *  their concrete USeinNavigationAsset subclass. Default: the abstract base
	 *  (use only if the subclass uses no serialized data — most don't). */
	virtual TSubclassOf<USeinNavigationAsset> GetAssetClass() const;

	/** Begin an async bake covering every ASeinNavVolume in World. Return true
	 *  if the bake kicked off (false if already running or no volumes found).
	 *  Subclasses own the async strategy — tick-driven, worker thread, whatever
	 *  fits their data. Progress reporting is the subclass's responsibility. */
	virtual bool BeginBake(UWorld* World) { return false; }

	/** True while an async bake is in progress. */
	virtual bool IsBaking() const { return false; }

	/** Request bake cancellation. Safe to call when not baking (no-op). */
	virtual void RequestCancelBake() {}

	// ----------------------------------------------------------------------
	// Runtime load — called on level begin-play
	// ----------------------------------------------------------------------

	/** Swap the loaded nav data. Passing nullptr clears runtime state.
	 *  Subclasses should call Super then broadcast OnNavigationMutated once
	 *  their own runtime arrays are updated — this base impl stores the
	 *  pointer but does NOT broadcast, so subclass mutations and the signal
	 *  stay in lockstep. */
	virtual void LoadFromAsset(USeinNavigationAsset* Asset) { LoadedAsset = Asset; }

	/** The currently-loaded baked asset, or nullptr if never loaded. */
	USeinNavigationAsset* GetLoadedAsset() const { return LoadedAsset; }

	/** True if the nav has usable runtime data (either from a baked asset or
	 *  procedurally initialized). Queries return no-path when false. */
	virtual bool HasRuntimeData() const { return LoadedAsset != nullptr; }

	// ----------------------------------------------------------------------
	// Queries — core pathing surface
	// ----------------------------------------------------------------------

	/** Synchronous path query. Subclasses override. Default: returns an invalid
	 *  path. */
	virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const { OutPath.Clear(); return false; }

	/** Fast reachability check. Default: falls back to FindPath. Subclasses
	 *  should override with a cheaper reachability component / flood-fill if
	 *  the FindPath cost is prohibitive on the query hot path (ability
	 *  validation runs this per activation). */
	virtual bool IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const;

	/** True if a world-space point is inside the nav's walkable region.
	 *  Default: false. Subclasses override. */
	virtual bool IsPassable(const FFixedVector& WorldPos) const { return false; }

	/** Snap an arbitrary world-space point to the nearest walkable location.
	 *  Returns false if no walkable point is within the nav's reachable region.
	 *  Default: passes the point through unchanged + returns HasRuntimeData(). */
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
	{
		OutProjected = WorldPos;
		return HasRuntimeData();
	}

	// ----------------------------------------------------------------------
	// Debug
	// ----------------------------------------------------------------------

	/** Per-frame debug draw hook. Called each tick while `ShowFlags.Navigation`
	 *  is on in any viewport. Default: no-op — the framework's shipped cell
	 *  viz goes through `USeinNavDebugComponent` (scene-proxy backed, one
	 *  batched mesh, editor-visible without PIE). Override only if you need
	 *  ephemeral per-frame drawing on top of that. */
	virtual void DrawDebug(UWorld* World) const {}

	/** Collect the nav's cell geometry for scene-proxy rendering. Subclasses
	 *  emit one quad per cell (XY-plane, slightly above the cell height) with
	 *  a per-cell color (green = walkable, red = blocked). Default: no cells.
	 *
	 *  Called by `USeinNavDebugComponent::CreateSceneProxy` when `ShowFlags.
	 *  Navigation` is on. The proxy captures the returned arrays once, so
	 *  this is NOT a per-frame hot path — only runs on load / bake / mutation.
	 *
	 *  In `UE_BUILD_SHIPPING` subclass overrides become no-ops (method bodies
	 *  are guarded by `UE_ENABLE_DEBUG_DRAWING`). The declaration stays for
	 *  vtable / ABI consistency. */
	virtual void CollectDebugCellQuads(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const {}

	/** Collect per-cell geometry for an active move-to path. Called each frame
	 *  from the debug ticker while `ShowFlags.Navigation` is on. Subclasses
	 *  rasterize the line from `AgentPos` through `Waypoints[CurrentIdx..End]`
	 *  into grid cells; the ticker draws them as tinted overlays.
	 *
	 *  - OutRemainingCells: cells along the path ahead of the agent (excludes
	 *    the current-target cell to avoid double-draw).
	 *  - OutCurrentTargetCell: the single cell containing `Waypoints[CurrentIdx]`
	 *    (drawn with a distinct lead-marker color). Empty if unreachable.
	 *
	 *  Same stripping convention as CollectDebugCellQuads. */
	virtual void CollectDebugPathCells(
		const FFixedVector& AgentPos,
		const TArray<FFixedVector>& Waypoints,
		int32 CurrentWaypointIndex,
		TArray<FVector>& OutRemainingCells,
		TArray<FVector>& OutCurrentTargetCell,
		float& OutHalfExtent) const {}

	// ----------------------------------------------------------------------
	// Events
	// ----------------------------------------------------------------------

	/** Broadcast after bake completion, asset swap, or dynamic obstacle mutation. */
	FSeinOnNavigationMutated OnNavigationMutated;

protected:

	/** The currently-loaded baked asset. Ownership stays with the volume / asset
	 *  registry — this is a non-owning pointer. */
	UPROPERTY(Transient)
	TObjectPtr<USeinNavigationAsset> LoadedAsset;
};
