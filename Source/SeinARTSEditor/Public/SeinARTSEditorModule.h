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
#include "UObject/StrongObjectPtr.h"

class IAssetTypeActions;
class USeinComponentCompilerExtension;
struct FGraphPanelPinFactory;

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
	TSharedPtr<FGraphPanelPinFactory> SeinPinFactory;
	// TStrongObjectPtr keeps the extension alive without relying on root-set
	// bookkeeping, and its destructor is safe during editor shutdown (unlike
	// a raw UObject* + RemoveFromRoot, which crashed when the UObject array
	// had already been torn down).
	TStrongObjectPtr<USeinComponentCompilerExtension> ComponentCompilerExtension;
	static EAssetTypeCategories::Type SeinARTSCategoryBit;
};
