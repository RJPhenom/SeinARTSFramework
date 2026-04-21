/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCellData.h
 * @brief   Compact per-cell data variants (Flat / Height / HeightSlope).
 *          Per DESIGN.md §13 "Compact cell data" — cell size is the memory-critical
 *          structure at RTS scale. Three struct variants plus an elevation mode enum.
 *          Each layer of USeinNavigationGrid stores exactly one of these arrays,
 *          chosen at bake time from USeinARTSCoreSettings::DefaultElevationMode
 *          (or per-NavVolume override).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/ElevationMode.h"
#include "SeinCellData.generated.h"

/** Per-cell walkability/status bits packed into FSeinCellData_*::Flags. */
namespace SeinCellFlag
{
	/** Cell has walkable geometry (produced by the bake trace). */
	static constexpr uint8 Walkable = 1 << 0;
	/** Cell is blocked by a dynamic entity footprint (registered at runtime). */
	static constexpr uint8 DynamicBlocked = 1 << 1;
	/** Cell has cover (designer/TerrainVolume stamped). */
	static constexpr uint8 HasCover = 1 << 2;
	/** Reserved for bake marker (e.g., "processed"). */
	static constexpr uint8 BakeProcessed = 1 << 3;
	/** Reserved framework bits 4..7 left open for future use (occupied by entity / special flags). */
}

/**
 * Flat cell — 4 bytes. Default mode.
 * Flags: walkability + cover-type + framework bits.
 * MovementCost: 0 = impassable; 1-255 = cost multiplier.
 * TagIndices: palette indices into USeinNavigationGrid::CellTagPalette (0 = null).
 *
 * Note: 2 inline tag slots keep the Flat struct at 4 bytes. DESIGN.md §13 proposes
 * 4 inline slots for fast-path capacity; that bumps cell size to 6 bytes (matching
 * HeightOnly). For MVP we ship 2 inline slots; extending to 4 is a one-line change
 * if profiling demands it.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinCellData_Flat
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Flags = 0;

	UPROPERTY()
	uint8 MovementCost = 0;

	UPROPERTY()
	uint8 TagIndexA = 0;

	UPROPERTY()
	uint8 TagIndexB = 0;

	FORCEINLINE bool IsWalkable() const { return (Flags & SeinCellFlag::Walkable) != 0 && !(Flags & SeinCellFlag::DynamicBlocked); }
	FORCEINLINE bool HasDynamicBlock() const { return (Flags & SeinCellFlag::DynamicBlocked) != 0; }
	FORCEINLINE void SetFlag(uint8 Flag, bool bValue) { if (bValue) { Flags |= Flag; } else { Flags = Flags & ~Flag; } }
};

/**
 * Cell + discrete height tier — 6 bytes. HeightTier is 0-255 quantized elevation.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinCellData_Height
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Flags = 0;

	UPROPERTY()
	uint8 MovementCost = 0;

	UPROPERTY()
	uint8 TagIndexA = 0;

	UPROPERTY()
	uint8 TagIndexB = 0;

	UPROPERTY()
	uint8 HeightTier = 0;

	UPROPERTY()
	uint8 Pad = 0;

	FORCEINLINE bool IsWalkable() const { return (Flags & SeinCellFlag::Walkable) != 0 && !(Flags & SeinCellFlag::DynamicBlocked); }
	FORCEINLINE bool HasDynamicBlock() const { return (Flags & SeinCellFlag::DynamicBlocked) != 0; }
	FORCEINLINE void SetFlag(uint8 Flag, bool bValue) { if (bValue) { Flags |= Flag; } else { Flags = Flags & ~Flag; } }
};

/**
 * Cell + fine-grained height + quantized slope — 8 bytes.
 * HeightFixed: int16 cm offset from layer baseline (FFixedPoint-derived at bake).
 * SlopeX/SlopeY: int8 components of unit-length slope vector, quantized to [-127, 127] = [-1.0, 1.0].
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinCellData_HeightSlope
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Flags = 0;

	UPROPERTY()
	uint8 MovementCost = 0;

	UPROPERTY()
	uint8 TagIndexA = 0;

	UPROPERTY()
	uint8 TagIndexB = 0;

	UPROPERTY()
	int16 HeightFixed = 0;

	UPROPERTY()
	int8 SlopeX = 0;

	UPROPERTY()
	int8 SlopeY = 0;

	FORCEINLINE bool IsWalkable() const { return (Flags & SeinCellFlag::Walkable) != 0 && !(Flags & SeinCellFlag::DynamicBlocked); }
	FORCEINLINE bool HasDynamicBlock() const { return (Flags & SeinCellFlag::DynamicBlocked) != 0; }
	FORCEINLINE void SetFlag(uint8 Flag, bool bValue) { if (bValue) { Flags |= Flag; } else { Flags = Flags & ~Flag; } }
};
