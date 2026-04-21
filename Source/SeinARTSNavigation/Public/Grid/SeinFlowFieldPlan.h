/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFlowFieldPlan.h
 * @brief   HPA*-driven flow-field plan spanning a source→goal route across
 *          clusters + optional navlink traversals. DESIGN.md §13 Nav composition:
 *          per-cluster flow fields are cached per CommandBroker order; consumed
 *          by USeinMovementProfile::AdvanceViaFlowField. Multi-layer transitions
 *          happen at navlink edges via the link's TraversalAbility.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Grid/SeinCellAddress.h"
#include "SeinFlowFieldPlan.generated.h"

/**
 * One step in an abstract-graph route: a cluster the unit enters + the edge
 * it will use to leave (or INDEX_NONE for the final cluster).
 */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinAbstractPathStep
{
	GENERATED_BODY()

	/** Layer this cluster is on. */
	UPROPERTY()
	uint8 Layer = 0;

	/** Cluster (tile) index on that layer. */
	UPROPERTY()
	int32 ClusterID = INDEX_NONE;

	/** Flat cell index inside the cluster this step targets (exit toward next cluster, or the final goal). */
	UPROPERTY()
	int32 GoalCellIndex = INDEX_NONE;

	/** AbstractGraph edge consumed to leave this cluster. INDEX_NONE for the final step. */
	UPROPERTY()
	int32 EdgeIndex = INDEX_NONE;

	/** When the outgoing edge is a navlink, index into USeinNavigationGrid::NavLinks (INDEX_NONE otherwise). */
	UPROPERTY()
	int32 NavLinkID = INDEX_NONE;

	/** Index into FSeinFlowFieldPlan::PerClusterFields, or INDEX_NONE if not yet computed. */
	UPROPERTY()
	int32 FlowFieldIndex = INDEX_NONE;
};

/**
 * Per-cluster flow field — directions pointing each cell toward the step's goal cell.
 * Direction codes: 0..7 compass (N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7).
 * 255 = unreachable.
 */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinClusterFlowField
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Layer = 0;

	UPROPERTY()
	int32 ClusterID = INDEX_NONE;

	/** Tile origin in whole-layer (X,Y) cell coords. */
	UPROPERTY()
	FIntPoint TileOriginXY = FIntPoint::ZeroValue;

	/** Tile extent in cells. */
	UPROPERTY()
	FIntPoint TileExtent = FIntPoint::ZeroValue;

	/** Cell this field is pointing toward (flat index within the whole layer). */
	UPROPERTY()
	int32 GoalCellIndex = INDEX_NONE;

	/** Direction codes per cell within the tile extent (row-major). */
	UPROPERTY()
	TArray<uint8> Directions;

	/** Sentinel for unreachable cells. */
	static constexpr uint8 DIR_UNREACHABLE = 255;
};

/**
 * Top-level plan — HPA* route + per-cluster flow fields. Cached by the
 * navigation subsystem per broker-order ID (or per start/goal pair for single-unit moves).
 */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinFlowFieldPlan
{
	GENERATED_BODY()

	/** Stable ID for the plan — cache key on the nav subsystem. */
	UPROPERTY()
	FGuid PlanID;

	UPROPERTY()
	FSeinCellAddress Start;

	UPROPERTY()
	FSeinCellAddress Goal;

	UPROPERTY()
	FGameplayTagContainer AgentTags;

	UPROPERTY()
	TArray<FSeinAbstractPathStep> Steps;

	UPROPERTY()
	TArray<FSeinClusterFlowField> PerClusterFields;

	UPROPERTY()
	bool bValid = false;

	/** Helper — is the step at `StepIndex` a navlink traversal hop? */
	bool IsStepNavLink(int32 StepIndex) const
	{
		return Steps.IsValidIndex(StepIndex) && Steps[StepIndex].NavLinkID != INDEX_NONE;
	}
};
