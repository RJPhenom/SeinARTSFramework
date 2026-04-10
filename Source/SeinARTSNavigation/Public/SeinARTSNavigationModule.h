#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSNavigationModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
