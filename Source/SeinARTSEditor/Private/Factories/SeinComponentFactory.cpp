/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentFactory.cpp
 */

#include "Factories/SeinComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Components/ActorComponents/SeinDynamicComponent.h"
#include "Components/ActorComponents/SeinComponentBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinComponentFactory::USeinComponentFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinComponentBlueprint::StaticClass();
	// Default to USeinDynamicComponent so a plain "Component" asset opens ready to
	// accept "Sein Sim Data" variables. Users can override via ConfigureProperties.
	ParentClass = USeinDynamicComponent::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinComponentFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinComponentBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);
}

bool USeinComponentFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickComponentParentClass", "Pick Parent Class for Component"),
		USeinActorComponent::StaticClass(),
		LOCTEXT("GenericComponent", "Sein Dynamic"),
		LOCTEXT("GenericComponentTip", "Create a Blueprint subclass of USeinDynamicComponent (new sim data type via BP variables)")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
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
