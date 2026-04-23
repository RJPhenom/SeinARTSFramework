/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.cpp
 */

#include "SeinNavigation.h"
#include "SeinNavigationAsset.h"

TSubclassOf<USeinNavigationAsset> USeinNavigation::GetAssetClass() const
{
	return USeinNavigationAsset::StaticClass();
}

bool USeinNavigation::IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const
{
	FSeinPathRequest Request;
	Request.Start = From;
	Request.End = To;
	Request.BlockedTerrainTags = AgentTags;
	FSeinPath Path;
	return FindPath(Request, Path) && Path.bIsValid;
}
