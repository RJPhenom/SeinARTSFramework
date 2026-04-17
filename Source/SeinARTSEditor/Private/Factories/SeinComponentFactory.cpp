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
#include "Settings/PluginSettings.h"
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
	UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(InParent, InName, Flags);
	if (NewStruct)
	{
		// Tag the struct so FSeinInstancedStructFilter can identify it as a
		// SeinARTS component in the Components picker. (We cannot reparent UDS
		// to FSeinComponent — UE's UDS compiler explicitly wipes SuperStruct
		// to nullptr during each compile, so the tag is how we discriminate.)
		NewStruct->SetMetaData(TEXT("SeinARTSComponent"), TEXT("true"));
	}
	return NewStruct;
}

FText USeinComponentFactory::GetDisplayName() const
{
	return LOCTEXT("SeinComponentFactoryDisplayName", "Component");
}

uint32 USeinComponentFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowComponentInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
