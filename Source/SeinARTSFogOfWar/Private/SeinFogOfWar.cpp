/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.cpp
 */

#include "SeinFogOfWar.h"
#include "SeinFogOfWarAsset.h"

TSubclassOf<USeinFogOfWarAsset> USeinFogOfWar::GetAssetClass() const
{
	return USeinFogOfWarAsset::StaticClass();
}
