/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActorFactory.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinActor Blueprint factory.
 */

#include "Factories/SeinActorFactory.h"
#include "SeinARTSEditorModule.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinActorFactory::USeinActorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinActorBlueprint::StaticClass();
	ParentClass = ASeinActor::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinActorFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinActorBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);
}

bool USeinActorFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickParentClass", "Pick Parent Class for Unit"),
		ASeinActor::StaticClass(),
		LOCTEXT("GenericUnit", "Generic Unit"),
		LOCTEXT("GenericUnitTip", "Create a Blueprint based on ASeinActor")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

FText USeinActorFactory::GetDisplayName() const
{
	return LOCTEXT("SeinActorFactoryDisplayName", "Unit");
}

uint32 USeinActorFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
