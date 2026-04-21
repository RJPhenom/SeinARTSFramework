/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityFactory.cpp
 */

#include "Factories/SeinEntityFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinEntityFactory::USeinEntityFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinActorBlueprint::StaticClass();
	ParentClass = ASeinActor::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinEntityFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinActorBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);
}

bool USeinEntityFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickEntityParentClass", "Pick Parent Class for Entity"),
		ASeinActor::StaticClass(),
		LOCTEXT("GenericEntity", "Generic Entity"),
		LOCTEXT("GenericEntityTip", "Create a bare Blueprint subclass of ASeinActor. Archetype Definition + Actor Bridge are auto-attached by the parent class.")
	);

	if (!ChosenClass) return false;
	ParentClass = ChosenClass;
	return true;
}

FText USeinEntityFactory::GetDisplayName() const
{
	return LOCTEXT("SeinEntityFactoryDisplayName", "SeinARTS Entity Actor");
}

uint32 USeinEntityFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowEntityInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
