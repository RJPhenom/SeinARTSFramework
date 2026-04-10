/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionComponent.cpp
 * @brief   Selection component implementation.
 */

#include "Player/SeinSelectionComponent.h"

USeinSelectionComponent::USeinSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USeinSelectionComponent::SetSelected(bool bNewSelected)
{
	if (bSelected == bNewSelected)
	{
		return;
	}

	bSelected = bNewSelected;

	if (bSelected)
	{
		ReceiveSelected();
	}
	else
	{
		ReceiveDeselected();
	}
}

void USeinSelectionComponent::SetHovered(bool bNewHovered)
{
	if (bHovered == bNewHovered)
	{
		return;
	}

	bHovered = bNewHovered;

	if (bHovered)
	{
		ReceiveHovered();
	}
	else
	{
		ReceiveUnhovered();
	}
}
