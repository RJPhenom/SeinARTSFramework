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
#include "Types/Quat.h"
#include "Types/Entity.h"
#include "Stamping/SeinStampShape.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.h"
#include "SeinNavigation.generated.h"

class UWorld;
class ASeinNavVolume;
class USeinNavigationAsset;

/**
 * One runtime path blocker = one FSeinStampShape posed at an entity. Multiple
 * stamps on the same entity produce multiple FSeinDynamicBlocker entries,
 * each carrying the same Owner so pathing self-exclusion works uniformly.
 *
 * Carrying the entity's transform (not just the stamp's world position) lets
 * the consumer apply LocalOffset/YawOffset deterministically inside their own
 * cell-iteration pass — the stamping system doesn't pre-compute world poses,
 * so blocker re-stamping at FindPath time stays consistent.
 *
 * Owner identifies the blocking entity so per-FindPath self-exclusion can
 * skip a unit's own blocker stamps — without it, a tank pathing from inside
 * its own footprint would never find a path out.
 */
struct FSeinDynamicBlocker
{
	FSeinStampShape Shape;
	FFixedVector EntityCenter;
	FFixedQuaternion EntityRotation;
	FSeinEntityHandle Owner;

	/** Layer mask of agents this blocker affects. Pathfinding gates via
	 *  intersection with the requesting agent's NavLayerMask. Default 0xFF
	 *  (blocks everyone) if the owning entity didn't author a mask. */
	uint8 BlockedNavLayerMask = 0xFF;
};

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

	/** Refresh the runtime dynamic-blocker set. Called by the nav stamping
	 *  system each PreTick from entities carrying FSeinExtentsData with
	 *  bBlocksNav set. Subclasses store the list and consume it during
	 *  FindPath (typically rebuilding a dynamic-blocked overlay per call
	 *  so the requester's own blocker can be excluded + the agent's layer
	 *  mask can filter out terrain that doesn't apply to this agent class).
	 *  Default: no-op — subclasses without dynamic blocker support are
	 *  unaffected. */
	virtual void SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& /*Blockers*/) {}

	/** Snap an arbitrary world-space point to the nearest walkable location.
	 *  Returns false if no walkable point is within the nav's reachable region.
	 *  Default: passes the point through unchanged + returns HasRuntimeData(). */
	virtual bool ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
	{
		OutProjected = WorldPos;
		return HasRuntimeData();
	}

	/** Sample the baked ground Z at a world-space XY. Returns false if the
	 *  XY is outside the nav's bounds. Blocked cells still return their stored
	 *  height (ground level under the obstacle, per the bake), so agents
	 *  clipping past edges don't pop to weird Zs.
	 *
	 *  Used by MoveToAction to keep the agent flush with local terrain as it
	 *  walks — sampling per-tick at the agent's *current* XY rather than
	 *  snapping Z to the next waypoint's Z (which causes teleport-to-target-Z
	 *  artifacts with stacked geometry). */
	virtual bool GetGroundHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const { return false; }

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

	/** Collect cells currently stamped by dynamic nav blockers (tanks,
	 *  vehicles, buildings under construction, etc.). Called per-frame from
	 *  the debug ticker while `ShowFlags.Navigation` is on so blocker stamps
	 *  appear live in the debug grid view. Emits one (Center, Color) pair
	 *  per cell — Color resolves from the blocker's BlockedNavLayerMask
	 *  against plugin-settings layer colors so the viz uniformly reflects
	 *  what each blocker is gating, regardless of whether the layer-
	 *  perspective filter is on. Subclasses with no dynamic-blocker support
	 *  emit nothing (default). Same shipping-strip convention as the other
	 *  debug collectors. */
	virtual void CollectDebugBlockerCells(
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
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
