/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.cpp
 * @brief   Implementation of navigation and pathfinding Blueprint nodes.
 */

#include "Lib/SeinNavigationBPFL.h"
#include "SeinPathfinder.h"
#include "SeinNavigationGrid.h"
#include "SeinNavigationSubsystem.h"
#include "Events/SeinVisualEvent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

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

// ---------------- Reachability + NavLink runtime mutation ----------------

static USeinNavigationGrid* ResolveGrid(const UObject* WorldContextObject)
{
	if (!WorldContextObject) { return nullptr; }
	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return nullptr; }
	if (USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>())
	{
		return Nav->GetGrid();
	}
	return nullptr;
}

bool USeinNavigationBPFL::SeinIsLocationReachable(
	const UObject* WorldContextObject,
	FFixedVector From,
	FFixedVector To,
	FGameplayTagContainer AgentTags)
{
	if (USeinNavigationGrid* Grid = ResolveGrid(WorldContextObject))
	{
		return Grid->IsLocationReachable(From, To, AgentTags);
	}
	return false;
}

bool USeinNavigationBPFL::SeinSetNavLinkEnabled(
	const UObject* WorldContextObject,
	int32 NavLinkID,
	bool bEnabled)
{
	USeinNavigationGrid* Grid = ResolveGrid(WorldContextObject);
	if (!Grid) { return false; }
	if (!Grid->SetNavLinkEnabled(NavLinkID, bEnabled)) { return false; }

	// Fire a NavLinkChanged visual event via the sim world subsystem so
	// render consumers can react (icon fade, etc.).
	if (UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr)
	{
		if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeNavLinkChangedEvent(NavLinkID, bEnabled));
		}
	}
	return true;
}

int32 USeinNavigationBPFL::SeinGetNavLinkCount(const UObject* WorldContextObject)
{
	if (USeinNavigationGrid* Grid = ResolveGrid(WorldContextObject))
	{
		return Grid->NavLinks.Num();
	}
	return 0;
}
