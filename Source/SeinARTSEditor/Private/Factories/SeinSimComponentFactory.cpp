/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimComponentFactory.cpp
 * @brief   Implementation of the UDS-based component factory.
 */

#include "Factories/SeinSimComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

const FName USeinSimComponentFactory::SeinDeterministicMetaKey(TEXT("SeinDeterministic"));

USeinSimComponentFactory::USeinSimComponentFactory()
{
	SupportedClass = UUserDefinedStruct::StaticClass();
	bCreateNew = FStructureEditorUtils::UserDefinedStructEnabled();
	bEditAfterNew = true;
}

UObject* USeinSimComponentFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ensure(UUserDefinedStruct::StaticClass() == Class);
	UUserDefinedStruct* NewUDS = FStructureEditorUtils::CreateUserDefinedStruct(InParent, Name, Flags);
	if (!NewUDS) return nullptr;

	// Tag the UDS at the struct level so both pin-type and struct-viewer filters
	// can detect it via UStruct::HasMetaData — same path that native USTRUCTs
	// marked `USTRUCT(meta = (SeinDeterministic))` use.
	NewUDS->SetMetaData(SeinDeterministicMetaKey, TEXT("true"));

	return NewUDS;
}

bool USeinSimComponentFactory::IsSeinDeterministicStruct(const UStruct* Struct)
{
	return Struct && Struct->HasMetaData(SeinDeterministicMetaKey);
}

FText USeinSimComponentFactory::GetDisplayName() const
{
	return LOCTEXT("SeinSimComponentFactoryDisplayName", "SeinARTS Component");
}

uint32 USeinSimComponentFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowComponentInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
