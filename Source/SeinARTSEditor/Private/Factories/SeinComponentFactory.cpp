/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinComponentFactory.cpp
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinARTS component struct factory.
 */

#include "Factories/SeinComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinComponentFactory::USeinComponentFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UUserDefinedStruct::StaticClass();
}

UObject* USeinComponentFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FStructureEditorUtils::CreateUserDefinedStruct(InParent, InName, Flags);
}

FText USeinComponentFactory::GetDisplayName() const
{
	return LOCTEXT("SeinComponentFactoryDisplayName", "Component");
}

uint32 USeinComponentFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
