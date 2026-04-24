/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.h
 * @brief   Infantry movement — basic seek+arrive plus face-velocity rotation.
 *
 *          Extends USeinBasicMovement. Units face the direction they're
 *          moving in, updated every tick. Turn is effectively instant (no
 *          turn-rate ramp) — matches CoH-style infantry where squad members
 *          pivot on the spot without a turn animation budget.
 *
 *          Planned extensions (tracked separately):
 *          - local separation / avoidance against other infantry
 *          - formation-slot maintenance when part of a squad
 *          Neither ships in the MVP pass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinBasicMovement.h"
#include "SeinInfantryMovement.generated.h"

UCLASS(meta = (DisplayName = "Infantry"))
class SEINARTSNAVIGATION_API USeinInfantryMovement : public USeinBasicMovement
{
	GENERATED_BODY()

public:
	virtual bool Tick(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveData,
		const FSeinPath& Path,
		int32& CurrentWaypointIndex,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		USeinNavigation* Nav) override;
};
