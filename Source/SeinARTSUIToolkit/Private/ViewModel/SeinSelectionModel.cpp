/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionModel.cpp
 * @brief   Selection model implementation.
 */

#include "ViewModel/SeinSelectionModel.h"
#include "ViewModel/SeinEntityViewModel.h"
#include "Core/SeinUISubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Actor/SeinActor.h"
#include "Engine/World.h"

void USeinSelectionModel::Initialize(USeinUISubsystem* InOwningSubsystem)
{
	OwningSubsystem = InOwningSubsystem;

	// Find the local player controller
	UWorld* World = InOwningSubsystem ? InOwningSubsystem->GetWorld() : nullptr;
	if (World)
	{
		CachedPlayerController = Cast<ASeinPlayerController>(World->GetFirstPlayerController());
		if (CachedPlayerController.IsValid())
		{
			CachedPlayerController->OnSelectionChanged.AddDynamic(this, &USeinSelectionModel::HandleSelectionChanged);
		}
	}
}

void USeinSelectionModel::Deinitialize()
{
	if (CachedPlayerController.IsValid())
	{
		CachedPlayerController->OnSelectionChanged.RemoveDynamic(this, &USeinSelectionModel::HandleSelectionChanged);
	}
	CachedPlayerController.Reset();
	SelectedViewModels.Empty();
}

void USeinSelectionModel::HandleSelectionChanged()
{
	RebuildFromController();
	OnSelectionChanged.Broadcast();
}

void USeinSelectionModel::RebuildFromController()
{
	SelectedViewModels.Empty();

	if (!CachedPlayerController.IsValid() || !OwningSubsystem.IsValid())
	{
		CachedFocusIndex = -1;
		return;
	}

	ASeinPlayerController* PC = CachedPlayerController.Get();
	USeinUISubsystem* Subsystem = OwningSubsystem.Get();

	CachedFocusIndex = PC->ActiveFocusIndex;

	TArray<ASeinActor*> SelectedActors = PC->GetValidSelectedActors();
	for (ASeinActor* Actor : SelectedActors)
	{
		if (Actor && Actor->HasValidEntity())
		{
			USeinEntityViewModel* VM = Subsystem->GetEntityViewModel(Actor->GetEntityHandle());
			if (VM)
			{
				SelectedViewModels.Add(VM);
			}
		}
	}
}

// ==================== Queries ====================

TArray<USeinEntityViewModel*> USeinSelectionModel::GetSelectedViewModels() const
{
	TArray<USeinEntityViewModel*> Result;
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (VM)
		{
			Result.Add(VM);
		}
	}
	return Result;
}

USeinEntityViewModel* USeinSelectionModel::GetFocusedViewModel() const
{
	if (CachedFocusIndex < 0 || CachedFocusIndex >= SelectedViewModels.Num())
	{
		return nullptr;
	}
	return SelectedViewModels[CachedFocusIndex];
}

USeinEntityViewModel* USeinSelectionModel::GetPrimaryViewModel() const
{
	// If a specific entity is focused, return that
	USeinEntityViewModel* Focused = GetFocusedViewModel();
	if (Focused)
	{
		return Focused;
	}

	// Otherwise return the first selected entity
	if (SelectedViewModels.Num() > 0 && SelectedViewModels[0])
	{
		return SelectedViewModels[0];
	}

	return nullptr;
}

int32 USeinSelectionModel::GetSelectionCount() const
{
	return SelectedViewModels.Num();
}

int32 USeinSelectionModel::GetActiveFocusIndex() const
{
	return CachedFocusIndex;
}

bool USeinSelectionModel::IsEntitySelected(FSeinEntityHandle Handle) const
{
	for (const TObjectPtr<USeinEntityViewModel>& VM : SelectedViewModels)
	{
		if (VM && VM->Entity == Handle)
		{
			return true;
		}
	}
	return false;
}

bool USeinSelectionModel::IsEntityFocused(FSeinEntityHandle Handle) const
{
	USeinEntityViewModel* Focused = GetFocusedViewModel();
	return Focused && Focused->Entity == Handle;
}
