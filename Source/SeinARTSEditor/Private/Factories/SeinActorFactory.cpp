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

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinActorFactory::USeinActorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = ASeinActor::StaticClass();
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
