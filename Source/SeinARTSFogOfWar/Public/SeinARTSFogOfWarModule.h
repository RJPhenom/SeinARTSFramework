#pragma once

#include "Modules/ModuleManager.h"
#include "Core/SeinPlayerID.h"

class FSeinARTSFogOfWarModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

namespace UE::SeinARTSFogOfWar
{
	/** Debug proxy observer override, driven by the
	 *  `SeinARTS.Debug.ShowFogOfWar.PlayerPerspective <id>` console command.
	 *  Writes OutObserver + returns true when an override is active (the
	 *  debug component should render that player's view instead of the
	 *  local PC's). Returns false in default state. Shipping: no-op false. */
	SEINARTSFOGOFWAR_API bool TryGetDebugObserverOverride(FSeinPlayerID& OutObserver);
}
