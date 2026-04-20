/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementProfileComponent.h
 * @brief   Sim component carrying a unit's movement profile class.
 *
 * Lives in the Navigation module (not CoreEntity) to avoid a circular
 * dependency between CoreEntity and Navigation. Add this component to a
 * unit's archetype alongside FSeinMovementData.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Templates/SubclassOf.h"
#include "Components/SeinComponent.h"
#include "SeinMovementProfileComponent.generated.h"

class USeinMovementProfile;

USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinMovementProfileComponent : public FSeinComponent
{
	GENERATED_BODY()

	/**
	 * Movement profile class controlling how this unit advances along a path.
	 * If null, USeinMoveToAction falls back to infantry-style behavior.
	 * Assign Infantry / Tracked / Wheeled profiles for different kinematics.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	TSubclassOf<USeinMovementProfile> Profile;
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementProfileComponent& Component)
{
	return GetTypeHash(Component.Profile.Get());
}
