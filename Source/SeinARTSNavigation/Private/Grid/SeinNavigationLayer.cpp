#include "Grid/SeinNavigationLayer.h"

void FSeinNavigationLayer::Allocate(int32 InWidth, int32 InHeight, ESeinElevationMode Mode, int32 InTileSize)
{
	Width = InWidth;
	Height = InHeight;
	ElevationMode = Mode;
	TileSize = InTileSize > 0 ? InTileSize : 32;

	const int32 Total = Width * Height;

	CellsFlat.Reset();
	CellsHeight.Reset();
	CellsHeightSlope.Reset();
	CellTagOverflow.Reset();

	switch (Mode)
	{
	case ESeinElevationMode::None:
		CellsFlat.SetNum(Total);
		break;
	case ESeinElevationMode::HeightOnly:
		CellsHeight.SetNum(Total);
		break;
	case ESeinElevationMode::HeightSlope:
		CellsHeightSlope.SetNum(Total);
		break;
	}

	const int32 TileCount = GetTileWidth() * GetTileHeight();
	Tiles.SetNum(TileCount);

	Clearance.SetNumZeroed(Total);
}

bool FSeinNavigationLayer::IsCellWalkable(int32 CellIndex) const
{
	if (!Clearance.IsValidIndex(CellIndex))
	{
		return false;
	}
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		return CellsFlat.IsValidIndex(CellIndex) && CellsFlat[CellIndex].IsWalkable();
	case ESeinElevationMode::HeightOnly:
		return CellsHeight.IsValidIndex(CellIndex) && CellsHeight[CellIndex].IsWalkable();
	case ESeinElevationMode::HeightSlope:
		return CellsHeightSlope.IsValidIndex(CellIndex) && CellsHeightSlope[CellIndex].IsWalkable();
	}
	return false;
}

uint8 FSeinNavigationLayer::GetCellFlags(int32 CellIndex) const
{
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		return CellsFlat.IsValidIndex(CellIndex) ? CellsFlat[CellIndex].Flags : 0;
	case ESeinElevationMode::HeightOnly:
		return CellsHeight.IsValidIndex(CellIndex) ? CellsHeight[CellIndex].Flags : 0;
	case ESeinElevationMode::HeightSlope:
		return CellsHeightSlope.IsValidIndex(CellIndex) ? CellsHeightSlope[CellIndex].Flags : 0;
	}
	return 0;
}

void FSeinNavigationLayer::SetCellFlags(int32 CellIndex, uint8 NewFlags)
{
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		if (CellsFlat.IsValidIndex(CellIndex)) CellsFlat[CellIndex].Flags = NewFlags;
		break;
	case ESeinElevationMode::HeightOnly:
		if (CellsHeight.IsValidIndex(CellIndex)) CellsHeight[CellIndex].Flags = NewFlags;
		break;
	case ESeinElevationMode::HeightSlope:
		if (CellsHeightSlope.IsValidIndex(CellIndex)) CellsHeightSlope[CellIndex].Flags = NewFlags;
		break;
	}
}

void FSeinNavigationLayer::SetCellFlag(int32 CellIndex, uint8 Flag, bool bValue)
{
	const uint8 Current = GetCellFlags(CellIndex);
	const uint8 Updated = bValue ? (Current | Flag) : (Current & ~Flag);
	if (Updated != Current)
	{
		SetCellFlags(CellIndex, Updated);
	}
}

uint8 FSeinNavigationLayer::GetCellMovementCost(int32 CellIndex) const
{
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		return CellsFlat.IsValidIndex(CellIndex) ? CellsFlat[CellIndex].MovementCost : 0;
	case ESeinElevationMode::HeightOnly:
		return CellsHeight.IsValidIndex(CellIndex) ? CellsHeight[CellIndex].MovementCost : 0;
	case ESeinElevationMode::HeightSlope:
		return CellsHeightSlope.IsValidIndex(CellIndex) ? CellsHeightSlope[CellIndex].MovementCost : 0;
	}
	return 0;
}

void FSeinNavigationLayer::SetCellMovementCost(int32 CellIndex, uint8 Cost)
{
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		if (CellsFlat.IsValidIndex(CellIndex)) CellsFlat[CellIndex].MovementCost = Cost;
		break;
	case ESeinElevationMode::HeightOnly:
		if (CellsHeight.IsValidIndex(CellIndex)) CellsHeight[CellIndex].MovementCost = Cost;
		break;
	case ESeinElevationMode::HeightSlope:
		if (CellsHeightSlope.IsValidIndex(CellIndex)) CellsHeightSlope[CellIndex].MovementCost = Cost;
		break;
	}
}

uint8 FSeinNavigationLayer::GetCellClearance(int32 CellIndex) const
{
	return Clearance.IsValidIndex(CellIndex) ? Clearance[CellIndex] : 0;
}

float FSeinNavigationLayer::GetCellWorldZ(int32 CellIndex) const
{
	switch (ElevationMode)
	{
	case ESeinElevationMode::None:
		return LayerBaselineZ;
	case ESeinElevationMode::HeightOnly:
		if (CellsHeight.IsValidIndex(CellIndex))
		{
			// HeightTier encodes 0-255 * step; MVP uses 1 cm per tier unit (adjust at bake to taste).
			return LayerBaselineZ + static_cast<float>(CellsHeight[CellIndex].HeightTier);
		}
		return LayerBaselineZ;
	case ESeinElevationMode::HeightSlope:
		if (CellsHeightSlope.IsValidIndex(CellIndex))
		{
			return LayerBaselineZ + static_cast<float>(CellsHeightSlope[CellIndex].HeightFixed);
		}
		return LayerBaselineZ;
	}
	return LayerBaselineZ;
}
