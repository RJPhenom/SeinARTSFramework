#pragma once

#include "CoreMinimal.h"
#include "SeinPathTypes.h"
#include "SeinNavigationGrid.h"
#include "SeinPathfinder.generated.h"

/**
 * Deterministic A* pathfinder operating on a USeinNavigationGrid.
 * All costs use integers for lockstep determinism.
 */
UCLASS()
class SEINARTSNAVIGATION_API USeinPathfinder : public UObject
{
	GENERATED_BODY()

public:
	/** Bind this pathfinder to a navigation grid. Must be called before FindPath. */
	void Initialize(USeinNavigationGrid* InGrid);

	/** Synchronous A* pathfinding for a single request. */
	FSeinPath FindPath(const FSeinPathRequest& Request);

	/**
	 * Process a batch of path requests up to a budget limit.
	 * @return Number of paths actually computed this call.
	 */
	int32 ProcessBatch(TArray<FSeinPathRequest>& Requests, TArray<FSeinPath>& OutPaths, int32 MaxPaths = 8);

private:
	UPROPERTY()
	TObjectPtr<USeinNavigationGrid> Grid;

	/** Internal node used during A* search. */
	struct FPathNode
	{
		FIntPoint Position;
		int32 GCost;        // Cost from start (integer for determinism)
		int32 HCost;        // Heuristic to end (integer)
		int32 FCost() const { return GCost + HCost; }
		FIntPoint Parent;
		bool bHasParent = false;
	};

	/** Octile distance heuristic scaled by 10 for integer arithmetic. */
	int32 HeuristicCost(FIntPoint A, FIntPoint B) const;

	/** Trace parent chain from End back to Start and build world-space path. */
	FSeinPath ReconstructPath(const TMap<int32, FPathNode>& NodeMap, FIntPoint Start, FIntPoint End, const FSeinPathRequest& Request);

	/** Remove collinear intermediate waypoints. */
	FSeinPath SmoothPath(const FSeinPath& RawPath);

	/** Check if a cell is passable for the given request (cost, terrain tags). */
	bool IsCellPassable(FIntPoint GridPos, const FSeinPathRequest& Request) const;
};
