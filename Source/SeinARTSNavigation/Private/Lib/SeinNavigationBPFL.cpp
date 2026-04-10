/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.cpp
 * @brief   Implementation of navigation and pathfinding Blueprint nodes.
 */

#include "Lib/SeinNavigationBPFL.h"
#include "SeinPathfinder.h"
#include "SeinNavigationGrid.h"

FSeinPath USeinNavigationBPFL::SeinRequestPath(USeinPathfinder* Pathfinder, FFixedVector Start, FFixedVector End, FSeinEntityHandle Requester, FGameplayTagContainer BlockedTerrainTags)
{
	if (!Pathfinder) return FSeinPath();

	FSeinPathRequest Request;
	Request.Start = Start;
	Request.End = End;
	Request.Requester = Requester;
	Request.BlockedTerrainTags = BlockedTerrainTags;

	return Pathfinder->FindPath(Request);
}

bool USeinNavigationBPFL::SeinIsPathValid(const FSeinPath& Path)
{
	return Path.bIsValid;
}

TArray<FFixedVector> USeinNavigationBPFL::SeinGetPathWaypoints(const FSeinPath& Path)
{
	return Path.Waypoints;
}

FFixedPoint USeinNavigationBPFL::SeinGetPathCost(const FSeinPath& Path)
{
	return Path.TotalCost;
}

int32 USeinNavigationBPFL::SeinGetPathLength(const FSeinPath& Path)
{
	return Path.GetWaypointCount();
}
