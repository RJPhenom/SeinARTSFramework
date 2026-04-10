/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SSeinActorPickerDialog.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Modal dialog for picking an ASeinActor parent class.
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Slate modal dialog for selecting an ASeinActor subclass.
 * Provides quick-select buttons for common base classes and a
 * full class hierarchy viewer for advanced selection.
 */
class SSeinActorPickerDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSeinActorPickerDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Open the modal dialog and return the selected class.
	 * @param InTitle - Title for the dialog window
	 * @return Selected UClass, or nullptr if cancelled
	 */
	static UClass* OpenDialog(const FText& InTitle);

private:
	UClass* SelectedClass = nullptr;
	TSharedPtr<SWindow> ParentWindow;
	bool bOKClicked = false;

	FReply OnQuickButtonClicked(UClass* InClass);
	FReply OnOKClicked();
	FReply OnCancelClicked();
	void OnClassPicked(UClass* InChosenClass);
};
