/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionComponent.h
 * @brief   Render-side selection state and visual hooks for ASeinActor.
 *          Purely visual — the sim never reads this component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SeinSelectionComponent.generated.h"

/**
 * Optional component on ASeinActor that tracks selection and hover state.
 * Provides BlueprintImplementableEvent hooks so designers can customize
 * selection circles, highlights, health bar visibility, etc. per unit type.
 *
 * This component lives entirely in the render layer. The simulation
 * does not know about selection.
 */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSFRAMEWORK_API USeinSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinSelectionComponent();

	// ========== State ==========

	/** Whether this actor is currently selected by the local player. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	bool bSelected = false;

	/** Whether the mouse cursor is currently hovering over this actor. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	bool bHovered = false;

	// ========== State Setters (called by player controller) ==========

	/** Set selected state and fire appropriate event. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void SetSelected(bool bNewSelected);

	/** Set hovered state and fire appropriate event. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void SetHovered(bool bNewHovered);

	// ========== Visual Events (override in BP) ==========

	/** Called when this actor becomes selected. Use to show selection circle, health bar, etc. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Selection", meta = (DisplayName = "On Selected"))
	void ReceiveSelected();

	/** Called when this actor is deselected. Use to hide selection visuals. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Selection", meta = (DisplayName = "On Deselected"))
	void ReceiveDeselected();

	/** Called when the mouse starts hovering over this actor. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Selection", meta = (DisplayName = "On Hovered"))
	void ReceiveHovered();

	/** Called when the mouse stops hovering over this actor. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Selection", meta = (DisplayName = "On Unhovered"))
	void ReceiveUnhovered();
};
