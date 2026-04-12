/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SSeinActorPickerDialog.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the actor class picker dialog.
 */

#include "Dialogs/SSeinActorPickerDialog.h"
#include "Actor/SeinActor.h"

#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

// ==================== Class Filter ====================

class FSeinActorClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass &&
			!InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden) &&
			InClass->IsChildOf(ASeinActor::StaticClass());
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(ASeinActor::StaticClass());
	}
};

// ==================== Widget ====================

void SSeinActorPickerDialog::Construct(const FArguments& InArgs)
{
	SelectedClass = ASeinActor::StaticClass();

	// Class viewer options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.ClassFilters.Add(MakeShared<FSeinActorClassFilter>());
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(
		Options,
		FOnClassPicked::CreateSP(this, &SSeinActorPickerDialog::OnClassPicked)
	);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// Quick buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("QuickSelect", "Quick Select"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f))

					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("GenericUnit", "Generic Unit"))
						.ToolTipText(LOCTEXT("GenericUnitTip", "Create a Blueprint based on ASeinActor"))
						.OnClicked(this, &SSeinActorPickerDialog::OnQuickButtonClicked, static_cast<UClass*>(ASeinActor::StaticClass()))
					]
				]
			]

			// Class viewer
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 0, 0, 8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AllClasses", "All Classes"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBox)
					.MinDesiredHeight(200.0f)
					[
						ClassViewer
					]
				]
			]

			// OK / Cancel
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked(this, &SSeinActorPickerDialog::OnOKClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(this, &SSeinActorPickerDialog::OnCancelClicked)
				]
			]
		]
	];
}

UClass* SSeinActorPickerDialog::OpenDialog(const FText& InTitle)
{
	TSharedRef<SSeinActorPickerDialog> Dialog = SNew(SSeinActorPickerDialog);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(InTitle)
		.ClientSize(FVector2D(400, 500))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	Window->SetContent(Dialog);
	Dialog->ParentWindow = Window;

	GEditor->EditorAddModalWindow(Window);

	if (Dialog->bOKClicked && Dialog->SelectedClass)
	{
		return Dialog->SelectedClass;
	}

	return nullptr;
}

FReply SSeinActorPickerDialog::OnQuickButtonClicked(UClass* InClass)
{
	SelectedClass = InClass;
	bOKClicked = true;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SSeinActorPickerDialog::OnOKClicked()
{
	bOKClicked = true;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SSeinActorPickerDialog::OnCancelClicked()
{
	bOKClicked = false;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void SSeinActorPickerDialog::OnClassPicked(UClass* InChosenClass)
{
	SelectedClass = InChosenClass;
}

#undef LOCTEXT_NAMESPACE
