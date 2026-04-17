/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionModel.h
 * @brief   Tracks the local player's selection state and provides entity
 *          ViewModels for selected units. Bridges the player controller's
 *          selection system with the UI ViewModel layer.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "SeinSelectionModel.generated.h"

class USeinEntityViewModel;
class USeinUISubsystem;
class ASeinPlayerController;

/** Broadcast when the selection or active focus changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSelectionModelChanged);

/**
 * Tracks the current selection and provides entity ViewModels for selected units.
 *
 * Owned by USeinUISubsystem. Automatically listens to the player controller's
 * OnSelectionChanged delegate and rebuilds its ViewModel list.
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinSelectionModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Selection Queries ==========

	/** Get ViewModels for all currently selected entities. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	TArray<USeinEntityViewModel*> GetSelectedViewModels() const;

	/**
	 * Get the ViewModel for the focused entity.
	 * Returns nullptr if active focus is "All" (-1).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	USeinEntityViewModel* GetFocusedViewModel() const;

	/**
	 * Get the "primary" ViewModel: the focused entity if one is focused,
	 * otherwise the first selected entity. Returns nullptr if nothing is selected.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Selection")
	USeinEntityViewModel* GetPrimaryViewModel() const;

	/** Get the number of currently selected entities. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	int32 GetSelectionCount() const;

	/** Get the active focus index (-1 = "All", 0+ = specific entity). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	int32 GetActiveFocusIndex() const;

	/** Check if a specific entity is in the current selection. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	bool IsEntitySelected(FSeinEntityHandle Handle) const;

	/** Check if a specific entity is the currently focused entity. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Selection")
	bool IsEntityFocused(FSeinEntityHandle Handle) const;

	// ========== Delegates ==========

	/** Fired when the selection or active focus changes. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Selection")
	FOnSelectionModelChanged OnSelectionChanged;

	// ========== Internal (called by USeinUISubsystem) ==========

	/** Initialize. Binds to the player controller's selection delegate if one is already available. */
	void Initialize(USeinUISubsystem* InOwningSubsystem);

	/** Unbind from delegates. */
	void Deinitialize();

	/**
	 * Bind to the local player controller's OnSelectionChanged if we haven't yet.
	 * Cheap no-op once bound. Called every sim tick by the UI subsystem so we
	 * catch the PC once it spawns (WorldSubsystem init runs before PC creation).
	 */
	void EnsurePlayerControllerBound();

private:
	/** Rebuild the ViewModel list from the current selection. */
	void RebuildFromController();

	/** Called when the player controller's selection changes. */
	UFUNCTION()
	void HandleSelectionChanged();

	/** Owning UI subsystem (for creating/getting entity ViewModels). */
	UPROPERTY()
	TWeakObjectPtr<USeinUISubsystem> OwningSubsystem;

	/** Cached player controller reference. */
	TWeakObjectPtr<ASeinPlayerController> CachedPlayerController;

	/** Current selection as entity ViewModels. */
	UPROPERTY()
	TArray<TObjectPtr<USeinEntityViewModel>> SelectedViewModels;

	/** Cached active focus index. */
	int32 CachedFocusIndex = -1;
};
