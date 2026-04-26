/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.h
 * @brief   Infantry movement — momentum-aware seek with smooth turn-to-velocity.
 *
 *          Inherits from USeinBasicMovement for class-hierarchy continuity but
 *          fully overrides Tick — does NOT call Super::Tick. Reasons:
 *          - Infantry needs scalar speed momentum (read/written through
 *            FSeinMovementData::CurrentSpeed) so reorders preserve velocity.
 *            Set Acceleration / Deceleration high (e.g. 50–100× MoveSpeed)
 *            on the unit's MoveData and the response is near-instant — but
 *            you don't get a hard zero-snap on order changes.
 *          - Basic stays dog-simple as the no-momentum baseline (good for
 *            cursors, debug agents). Infantry is the smoothed variant.
 *
 *          Rotation: face the velocity vector, ramping yaw at MoveData.TurnRate.
 *          Skip rotation when planar velocity is sub-epsilon so blocked /
 *          arrival ticks don't slew facing back to identity.
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
	virtual void OnMoveBegin(const FSeinLocomotionContext& Ctx) override;
	virtual bool Tick(const FSeinLocomotionContext& Ctx) override;
};
