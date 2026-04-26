/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsComponentVisualizer.h
 * @brief   Component visualizer that draws FSeinExtentsData shapes (Box /
 *          Capsule) in the BP editor + level editor viewports so designers
 *          can see an entity's volumetric bounds at a glance. Read-only —
 *          all editing happens through the Details panel.
 *
 *          Each shape's pose composes from the actor transform + LocalOffset
 *          (rotated by actor yaw) + YawOffset; per-shape Height drives
 *          vertical extent. Yellow box for Box shapes, cyan capsule for
 *          Capsule shapes.
 */

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class FSeinExtentsComponentVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(
		const UActorComponent* Component,
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;
};
