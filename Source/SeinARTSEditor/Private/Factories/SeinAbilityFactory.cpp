/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbilityFactory.cpp
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinAbility Blueprint factory.
 */

#include "Factories/SeinAbilityFactory.h"
#include "SeinARTSEditorModule.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Abilities/SeinAbility.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinAbilityFactory::USeinAbilityFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = USeinAbility::StaticClass();
}

bool USeinAbilityFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickAbilityParentClass", "Pick Parent Class for Ability"),
		USeinAbility::StaticClass(),
		LOCTEXT("GenericAbility", "Generic Ability"),
		LOCTEXT("GenericAbilityTip", "Create a Blueprint based on USeinAbility")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

FText USeinAbilityFactory::GetDisplayName() const
{
	return LOCTEXT("SeinAbilityFactoryDisplayName", "Ability");
}

uint32 USeinAbilityFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
