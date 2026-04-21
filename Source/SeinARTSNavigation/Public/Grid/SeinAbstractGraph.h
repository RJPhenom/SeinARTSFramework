/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbstractGraph.h
 * @brief   HPA*-style abstract-graph storage (DESIGN.md §13 "Nav composition"
 *          + "NavLinks"). Nodes = cluster centroids; Edges = inter-cluster
 *          connections (cluster-border or navlink). Pathfinder + reachability
 *          query operate on this graph.
 *
 *          MVP simplification: one node per cluster (cluster == tile). Proper
 *          entrance-per-border nodes arrive when Session 3.3 flow fields
 *          need per-cluster entrance positions.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Grid/SeinCellAddress.h"
#include "SeinAbstractGraph.generated.h"

class USeinAbility;

UENUM()
enum class ESeinAbstractEdgeType : uint8
{
	/** Edge between adjacent clusters (standard walkable transition). */
	ClusterBorder,
	/** Edge produced by an ASeinNavLinkProxy (vault / drop / climb / custom). */
	NavLink,
};

/** Abstract-graph node — one per cluster in the MVP; per-border-entrance later. */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinAbstractNode
{
	GENERATED_BODY()

	/** Cluster this node represents (for MVP node-per-cluster, this is 1:1). */
	UPROPERTY()
	int32 ClusterID = INDEX_NONE;

	/** Layer this node lives on. */
	UPROPERTY()
	uint8 Layer = 0;

	/** Representative cell inside the cluster (e.g., cluster centroid walkable cell). */
	UPROPERTY()
	int32 RepresentativeCellIndex = INDEX_NONE;

	/** Indices into FSeinAbstractGraph::Edges that originate here. */
	UPROPERTY()
	TArray<int32> OutgoingEdgeIndices;
};

/** Abstract-graph edge — carries cost + type + traversal/eligibility for navlinks. */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinAbstractEdge
{
	GENERATED_BODY()

	UPROPERTY()
	int32 FromNode = INDEX_NONE;

	UPROPERTY()
	int32 ToNode = INDEX_NONE;

	/** Integer cost (world-unit-scaled distance; higher for navlinks via AdditionalCost). */
	UPROPERTY()
	int32 Cost = 0;

	UPROPERTY()
	ESeinAbstractEdgeType EdgeType = ESeinAbstractEdgeType::ClusterBorder;

	/** Populated iff EdgeType == NavLink. */
	UPROPERTY()
	TSubclassOf<USeinAbility> TraversalAbility;

	/** Populated iff EdgeType == NavLink (empty query = any entity accepts). */
	UPROPERTY()
	FGameplayTagQuery EligibilityQuery;

	/** For NavLink edges: index into USeinNavigationGrid::NavLinks (INDEX_NONE otherwise). */
	UPROPERTY()
	int32 NavLinkID = INDEX_NONE;

	/** Runtime flag — set to false by SeinSetNavLinkEnabled to skip this edge. */
	UPROPERTY()
	bool bEnabled = true;
};

/** Abstract-graph storage — owned by USeinNavigationGrid. */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinAbstractGraph
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinAbstractNode> Nodes;

	UPROPERTY()
	TArray<FSeinAbstractEdge> Edges;

	/** Clear all graph state. */
	void Reset()
	{
		Nodes.Reset();
		Edges.Reset();
	}

	/** Find the node index for (Layer, ClusterID). Returns INDEX_NONE if not present. */
	int32 FindNode(uint8 Layer, int32 ClusterID) const
	{
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (Nodes[i].Layer == Layer && Nodes[i].ClusterID == ClusterID)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
};

/**
 * Baked record of one ASeinNavLinkProxy — persists on the grid asset so runtime
 * can look up the proxy by index for enable/disable toggles without re-bake.
 */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinNavLinkRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinCellAddress StartCell;

	UPROPERTY()
	FSeinCellAddress EndCell;

	UPROPERTY()
	int32 AdditionalCost = 0;

	UPROPERTY()
	bool bBidirectional = true;

	UPROPERTY()
	FGameplayTagQuery EligibilityQuery;

	UPROPERTY()
	TSubclassOf<USeinAbility> TraversalAbility;

	/** Soft-ref for runtime lookup (SeinSetNavLinkEnabled sourced from an actor). */
	UPROPERTY()
	FSoftObjectPath SourceActorPath;

	/** Default-enabled at bake; SeinSetNavLinkEnabled flips at runtime. */
	UPROPERTY()
	bool bEnabled = true;

	/** Edge indices in AbstractGraph.Edges backing this navlink (1 or 2 depending on bidirectional). */
	UPROPERTY()
	TArray<int32> EdgeIndices;
};
