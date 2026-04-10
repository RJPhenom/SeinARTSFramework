/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorModule.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Editor module for SeinARTS content creation tools.
 */

#pragma once

#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"

class IAssetTypeActions;

class FSeinARTSEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static EAssetTypeCategories::Type GetAssetCategoryBit();

private:
	void RegisterAssetTypeActions();
	void UnregisterAssetTypeActions();

	TArray<TSharedPtr<IAssetTypeActions>> RegisteredActions;
	static EAssetTypeCategories::Type SeinARTSCategoryBit;
};
