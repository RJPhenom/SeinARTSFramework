/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorStyle.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Slate style set for the SeinARTS editor module.
 */

#pragma once

#include "Styling/SlateStyle.h"

/**
 * Manages the Slate style set for SeinARTS editor tooling.
 * Registers class icons, thumbnails, and branding assets.
 */
class FSeinARTSEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const FSlateStyleSet& Get();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
