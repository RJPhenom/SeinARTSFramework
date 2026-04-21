/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavDebugRenderingComponent.h
 * @brief   Scene-proxy-backed nav debug drawer, modelled on UE's own
 *          UNavMeshRenderingComponent (NavMeshRenderingComponent.cpp). The
 *          proxy's `GetViewRelevance` checks `EngineShowFlags.Navigation` per
 *          view, so UE's `P` chord, the Show > Navigation menu, and our own
 *          `SeinARTS.Navigation.Debug` console command all drive it — each
 *          viewport toggles independently, matching native navmesh behaviour.
 *
 *          Stripped from shipping via `UE_ENABLE_DEBUG_DRAWING`.
 */

#pragma once

#include "CoreMinimal.h"
#include "Debug/DebugDrawComponent.h"
#include "SeinNavDebugRenderingComponent.generated.h"

UCLASS(ClassGroup = (SeinARTS), MinimalAPI,
       hidecategories = (Activation, Collision, ComponentTick, ComponentReplication, Cooking, Events, Physics, LOD, Navigation, Replication, Tags, Trigger, AssetUserData, Sockets, Mobility))
class USeinNavDebugRenderingComponent : public UDebugDrawComponent
{
	GENERATED_BODY()

public:
	USeinNavDebugRenderingComponent();

	/** Queue a proxy rebuild on the next editor-timer tick (call after a bake completes or a
	 *  dynamic cell-mutation BPFL ran so the viz reflects new data). */
	void ForceUpdate() { bForceUpdate = true; }

	/** True when any relevant viewport has ShowFlags.Navigation set (matches the gate used
	 *  by the scene proxy's GetViewRelevance, so the timer's re-bake trigger stays in sync). */
	static bool IsNavigationShowFlagSet(const UWorld* World);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

#if UE_ENABLE_DEBUG_DRAWING
	virtual FDebugRenderSceneProxy* CreateDebugSceneProxy() override;
#endif

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	/** 1 Hz timer that watches the nav show flag + ForceUpdate and MarkRenderStateDirty's
	 *  the component on an ON transition (so the proxy rebuilds with current data). UE's
	 *  UNavMeshRenderingComponent uses the same pattern (comment there: "there is no event
	 *  or other information that show flag was changed by user"). */
	void TimerFunction();

	FTimerHandle TimerHandle;
	bool bForceUpdate = false;
	bool bCollectDebugData = false;
};
