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
	UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(InParent, InName, Flags);
	if (NewStruct)
	{
		// Tag the struct so the thumbnail renderer can identify SeinARTS components
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
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
