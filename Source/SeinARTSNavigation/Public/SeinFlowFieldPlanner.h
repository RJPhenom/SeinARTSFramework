/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFlowFieldPlanner.h
 * @brief   Builds FSeinFlowFieldPlan from (start, goal) pairs using HPA* abstract
 *          graph A* for cluster routing + per-cluster Dijkstra flow fields. Owned
 *          by USeinNavigationSubsystem with a simple key-based plan cache.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Grid/SeinFlowFieldPlan.h"
#include "SeinFlowFieldPlanner.generated.h"

class USeinNavigationGrid;

UCLASS()
class SEINARTSNAVIGATION_API USeinFlowFieldPlanner : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to a grid. Must be called before any BuildPlan call. */
	void Initialize(USeinNavigationGrid* InGrid);

	/**
	 * Build a full plan: HPA* abstract-graph A* from the start cluster to the goal
	 * cluster, then per-cluster Dijkstra flow fields. Returns an invalid plan
	 * (bValid=false) if the goal is unreachable or either endpoint is invalid.
	 */
	FSeinFlowFieldPlan BuildPlan(
		const FSeinCellAddress& Start,
		const FSeinCellAddress& Goal,
		const FGameplayTagContainer& AgentTags);

	/** Cache key for single-unit moves. */
	static FGuid KeyForCells(const FSeinCellAddress& Start, const FSeinCellAddress& Goal);

	/** Lookup-or-build pattern. */
	FSeinFlowFieldPlan& GetOrBuild(
		const FGuid& Key,
		const FSeinCellAddress& Start,
		const FSeinCellAddress& Goal,
		const FGameplayTagContainer& AgentTags);

	/** Evict a cached plan by key. */
	void InvalidatePlan(const FGuid& Key);

	/** Evict all cached plans (called on nav-grid mutation for MVP — precise per-cluster invalidation later). */
	void InvalidateAll();

private:
	UPROPERTY()
	TObjectPtr<USeinNavigationGrid> Grid;

	UPROPERTY()
	TMap<FGuid, FSeinFlowFieldPlan> PlanCache;

	/** HPA* abstract-graph A*: returns the sequence of nodes traversed. */
	bool RouteAbstract(
		int32 StartNodeIdx,
		int32 GoalNodeIdx,
		const FGameplayTagContainer& AgentTags,
		TArray<int32>& OutNodeSequence,
		TArray<int32>& OutEdgeSequence) const;

	/** Dijkstra sweep within a cluster. Writes `OutField.Directions`. */
	void ComputeClusterField(
		const FSeinAbstractPathStep& Step,
		FSeinClusterFlowField& OutField) const;
};
