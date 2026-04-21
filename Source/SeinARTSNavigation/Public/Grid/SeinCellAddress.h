/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCellAddress.h
 * @brief   Addressing primitive for multi-layer nav grids (DESIGN.md §13
 *          "Multi-layer navigation"). Pairs layer index with a flat 1D cell
 *          index within that layer. Single-layer callers use Layer = 0.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinCellAddress.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSNAVIGATION_API FSeinCellAddress
{
	GENERATED_BODY()

	/** Layer index (0 = ground, higher stacks upward). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Grid")
	uint8 Layer = 0;

	/** Flat 1D index into the layer's cell array = Y * Width + X. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Grid")
	int32 CellIndex = INDEX_NONE;

	FSeinCellAddress() = default;
	FSeinCellAddress(uint8 InLayer, int32 InCellIndex) : Layer(InLayer), CellIndex(InCellIndex) {}

	FORCEINLINE bool IsValid() const { return CellIndex >= 0; }

	FORCEINLINE bool operator==(const FSeinCellAddress& Other) const { return Layer == Other.Layer && CellIndex == Other.CellIndex; }
	FORCEINLINE bool operator!=(const FSeinCellAddress& Other) const { return !(*this == Other); }

	static FSeinCellAddress Invalid() { return FSeinCellAddress(0, INDEX_NONE); }

	friend FORCEINLINE uint32 GetTypeHash(const FSeinCellAddress& Addr)
	{
		return HashCombine(::GetTypeHash(Addr.Layer), ::GetTypeHash(Addr.CellIndex));
	}
};
