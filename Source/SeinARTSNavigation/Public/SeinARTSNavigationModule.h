#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSNavigationModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

#if UE_ENABLE_DEBUG_DRAWING
namespace UE::SeinARTSNavigation
{
	/** True iff any viewport currently has the SeinSteeringVectors custom
	 *  show flag enabled. Module-public so USeinLocomotion's avoidance path
	 *  can gate its per-tick debug draw on the flag without needing a
	 *  locomotion-specific console var. */
	SEINARTSNAVIGATION_API bool IsSteeringVectorsShowFlagOn();

	/** Read the current `Sein.Nav.Show.Layer` console override. Returns true
	 *  + writes the selected bit (0..7) when active; false when no override
	 *  is set (reset state — show all blockers). USeinNavigationAStar reads
	 *  this from CollectDebugBlockerCells to filter the rendered orange
	 *  cells to those that affect the chosen agent layer. */
	SEINARTSNAVIGATION_API bool TryGetDebugNavLayerOverride(int32& OutBitIndex);
}
#endif
