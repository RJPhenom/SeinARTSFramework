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
#include "Abilities/SeinAbilityBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinAbilityFactory::USeinAbilityFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinAbilityBlueprint::StaticClass();
	ParentClass = USeinAbility::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinAbilityFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinAbilityBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);

	// Seed the event graph with the core ability lifecycle events so designers
	// get a starting point identical to how Actor BPs ship with BeginPlay/Tick.
	if (NewBP)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(NewBP);
		if (EventGraph)
		{
			int32 NodePosY = 0;
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnActivate")), USeinAbility::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnTick")),     USeinAbility::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnEnd")),      USeinAbility::StaticClass(), NodePosY);
		}
	}

	return NewBP;
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
