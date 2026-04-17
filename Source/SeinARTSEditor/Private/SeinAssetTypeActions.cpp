/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAssetTypeActions.cpp
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinARTS asset type actions.
 */

#include "SeinAssetTypeActions.h"
#include "SeinARTSEditorModule.h"
#include "Actor/SeinActorBlueprint.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Components/ActorComponents/SeinComponentBlueprint.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

// ==================== Unit (SeinActorBlueprint) ====================

FText FAssetTypeActions_SeinActorBlueprint::GetName() const
{
	return LOCTEXT("SeinActorBlueprintName", "SeinARTS Unit");
}

UClass* FAssetTypeActions_SeinActorBlueprint::GetSupportedClass() const
{
	return USeinActorBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinActorBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Ability (SeinAbilityBlueprint) ====================

FText FAssetTypeActions_SeinAbilityBlueprint::GetName() const
{
	return LOCTEXT("SeinAbilityBlueprintName", "SeinARTS Ability");
}

UClass* FAssetTypeActions_SeinAbilityBlueprint::GetSupportedClass() const
{
	return USeinAbilityBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinAbilityBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Component (SeinComponentBlueprint) ====================

FText FAssetTypeActions_SeinComponentBlueprint::GetName() const
{
	return LOCTEXT("SeinComponentBlueprintName", "SeinARTS Component");
}

UClass* FAssetTypeActions_SeinComponentBlueprint::GetSupportedClass() const
{
	return USeinComponentBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinComponentBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
