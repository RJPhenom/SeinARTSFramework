/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementModule.h
 * @brief   Module class for SeinARTSMovement.
 *
 *          Owns the steering-vectors show flag (`Sein.Nav.Show.SteeringVectors`)
 *          and the active-move path debug ticker. The cell-quad floor and the
 *          `Sein.Nav.Show` toggle stay in SeinARTSNavigation — both modules
 *          gate on that one toggle so designers see one unified "show nav debug"
 *          UX, even though the rendering work is split across two modules.
 *
 *          Decoupling rationale: movement + the move-to action moved here
 *          (out of SeinARTSNavigation) so the navigation impl is fully
 *          replaceable. The movement layer consumes navigation through
 *          USeinNavigation's abstract surface only.
 */

#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSMovementModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

#if UE_ENABLE_DEBUG_DRAWING
class UWorld;

namespace UE::SeinARTSMovement
{
	/** True iff some viewport rendering `World` currently has the
	 *  SeinSteeringVectors custom show flag enabled. Per-world matching
	 *  (mirrors `UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld`)
	 *  so that toggling the flag on/off on the user's visible PIE viewport
	 *  reflects in the per-tick steering arrow draw, even when other
	 *  viewports (editor / other PIE windows) have the flag in the
	 *  opposite state.
	 *
	 *  Module-public so USeinMovement's avoidance path can gate its
	 *  per-tick debug draw without needing a movement-specific console var. */
	SEINARTSMOVEMENT_API bool IsSteeringVectorsShowFlagOnForWorld(UWorld* World);
}
#endif
