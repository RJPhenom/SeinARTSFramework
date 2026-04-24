#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSFogOfWarModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
