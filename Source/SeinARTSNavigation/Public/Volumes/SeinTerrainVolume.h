/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainVolume.h
 * @brief   Tag-stamping volume actor (DESIGN.md §13 "Authoring surface").
 *          Designer-subclassable base that declares a set of cell tags to
 *          stamp over the cells its brush covers. Runs after the nav bake via
 *          OnBake(grid).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinTerrainVolume.generated.h"

class USeinNavigationGrid;

UCLASS(Blueprintable, hidecategories = (Collision, Brush, Attachment, Physics, Volume),
       meta = (DisplayName = "Sein Terrain Volume"))
class SEINARTSNAVIGATION_API ASeinTerrainVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinTerrainVolume(const FObjectInitializer& ObjectInitializer);

	/** Tags to stamp onto every cell covered by this volume's brush. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Terrain")
	FGameplayTagContainer CoverTags;

	/**
	 * Optional cover-facing direction — unit vector designers set for directional
	 * cover (e.g., "this cover protects against attacks from the +X direction").
	 * Empty (zero) = non-directional. Consumed by USeinTerrainBPFL::SeinQueryCoverAtLocation.
	 */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Terrain")
	FFixedVector CoverFacingDirection;

	/**
	 * Optional per-volume movement cost override. When > 0, cells covered by this
	 * volume's brush get their movement cost bumped to this value at bake time.
	 * Zero = leave movement cost untouched.
	 */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Terrain", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 MovementCostOverride = 0;

	/** World-space AABB of this volume's brush. */
	FBox GetVolumeWorldBounds() const;
};
