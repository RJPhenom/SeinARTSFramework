/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavLinkProxy.h
 * @brief   Placed-in-level navlink actor (DESIGN.md §13 "NavLinks"). Designer
 *          drops it into a level, positions the two endpoint scene components
 *          (Start / End), assigns a TraversalAbility class. At bake time the
 *          endpoints resolve to FSeinCellAddress and an abstract-graph edge is
 *          added carrying the traversal ability + eligibility query.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SeinNavLinkProxy.generated.h"

class USceneComponent;
class UBillboardComponent;
class USeinAbility;

UCLASS(meta = (DisplayName = "Sein Nav Link Proxy"))
class SEINARTSNAVIGATION_API ASeinNavLinkProxy : public AActor
{
	GENERATED_BODY()

public:
	ASeinNavLinkProxy();

	/** Root scene (actor transform). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|NavLink|Components")
	TObjectPtr<USceneComponent> Root;

	/** Start endpoint anchor — position via the editor gizmo. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|NavLink|Components")
	TObjectPtr<USceneComponent> StartAnchor;

	/** End endpoint anchor — position via the editor gizmo. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|NavLink|Components")
	TObjectPtr<USceneComponent> EndAnchor;

#if WITH_EDITORONLY_DATA
	/** Visual marker for the Start endpoint in the editor. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> StartSprite;

	/** Visual marker for the End endpoint in the editor. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EndSprite;
#endif

	/** Ability that plays when a unit traverses this link (vault, drop, climb, etc.). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|NavLink")
	TSubclassOf<USeinAbility> TraversalAbility;

	/** Additional cost added to the base cell-to-cell cost for this traversal. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|NavLink", meta = (ClampMin = "0", ClampMax = "65535"))
	int32 AdditionalCost = 0;

	/** True if the link can be traversed in either direction. False = Start → End only. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|NavLink")
	bool bBidirectional = true;

	/**
	 * Eligibility filter — a pathing entity's tags must match this query to consider
	 * the link. Empty query = any entity accepts. Example: infantry-only vaults,
	 * vehicle-only bridges.
	 */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|NavLink")
	FGameplayTagQuery EligibilityQuery;

	/** World-space Start endpoint transform. */
	FTransform GetStartWorldTransform() const;

	/** World-space End endpoint transform. */
	FTransform GetEndWorldTransform() const;

	virtual void OnConstruction(const FTransform& Transform) override;
};
