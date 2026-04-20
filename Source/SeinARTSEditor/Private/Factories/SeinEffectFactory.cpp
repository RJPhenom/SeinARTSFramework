/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEffectFactory.cpp
 * @brief:		Implementation of SeinEffect Blueprint factory.
 */

#include "Factories/SeinEffectFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Effects/SeinEffect.h"
#include "Effects/SeinEffectBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinEffectFactory::USeinEffectFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinEffectBlueprint::StaticClass();
	ParentClass = USeinEffect::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinEffectFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinEffectBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);

	// Seed the event graph with the core effect lifecycle hooks so designers
	// open onto a familiar starting point — same convention as the ability factory.
	if (NewBP)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(NewBP);
		if (EventGraph)
		{
			int32 NodePosY = 0;
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnApply")),   USeinEffect::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnTick")),    USeinEffect::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnExpire")),  USeinEffect::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnRemoved")), USeinEffect::StaticClass(), NodePosY);
		}
	}

	return NewBP;
}

bool USeinEffectFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickEffectParentClass", "Pick Parent Class for Effect"),
		USeinEffect::StaticClass(),
		LOCTEXT("GenericEffect", "Generic Effect"),
		LOCTEXT("GenericEffectTip", "Create a Blueprint based on USeinEffect")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

FText USeinEffectFactory::GetDisplayName() const
{
	return LOCTEXT("SeinEffectFactoryDisplayName", "Effect");
}

uint32 USeinEffectFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowEffectInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
