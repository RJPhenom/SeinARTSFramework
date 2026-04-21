/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionGroup.h
 * @brief   Per-VisionGroup storage (DESIGN.md §12). Holds per-nav-layer
 *          visibility + explored bitfields; one VisionGroup per player
 *          (Private scope) or per team (TeamShared scope).
 *
 *          MVP ships uint8 "visible now" + uint8 "explored" flags per cell —
 *          full 4-bit packed refcounts for proper stamp-delta land in a
 *          polish pass once vision perf shows up in profiling.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinVisionGroup.generated.h"

/** Per-nav-layer vision bitfields for a single VisionGroup. */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinVisionGroupLayer
{
	GENERATED_BODY()

	/** Vision-grid width in cells (derived from nav grid × VisionCellsPerNavCell scale). */
	UPROPERTY()
	int32 Width = 0;

	/** Vision-grid height in cells. */
	UPROPERTY()
	int32 Height = 0;

	/** Per-cell visible flag (0 = not visible, 1 = visible right now). */
	UPROPERTY()
	TArray<uint8> Visible;

	/** Per-cell explored flag (0 = never seen, 1 = has been visible at least once). */
	UPROPERTY()
	TArray<uint8> Explored;

	void Allocate(int32 InWidth, int32 InHeight)
	{
		Width = InWidth;
		Height = InHeight;
		const int32 Count = FMath::Max(0, Width * Height);
		Visible.SetNumZeroed(Count);
		Explored.SetNumZeroed(Count);
	}

	/** Zero the Visible bitfield at the start of each vision tick. Explored keeps accumulating. */
	void ClearVisible()
	{
		FMemory::Memzero(Visible.GetData(), Visible.Num());
	}

	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValid(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
};

/** One VisionGroup — per player (Private) or per team (TeamShared). */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinVisionGroup
{
	GENERATED_BODY()

	/** Per-nav-layer vision state. Sized to the nav grid's layer count. */
	UPROPERTY()
	TArray<FSeinVisionGroupLayer> Layers;
};
