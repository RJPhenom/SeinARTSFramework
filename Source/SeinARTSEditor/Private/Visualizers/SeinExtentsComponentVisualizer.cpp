/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsComponentVisualizer.cpp
 */

#include "Visualizers/SeinExtentsComponentVisualizer.h"

#include "Components/ActorComponents/SeinExtentsComponent.h"
#include "Components/SeinExtentsData.h"

#include "GameFramework/Actor.h"
#include "SceneManagement.h"

void FSeinExtentsComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* /*View*/,
	FPrimitiveDrawInterface* PDI)
{
	const USeinExtentsComponent* ExtComp = Cast<USeinExtentsComponent>(Component);
	if (!ExtComp || !PDI) return;
	if (ExtComp->Data.Shapes.Num() == 0) return;

	const AActor* Owner = ExtComp->GetOwner();
	const FTransform ActorXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	const FQuat ActorQuat = ActorXform.GetRotation();
	const FVector ActorPos = ActorXform.GetLocation();

	// Yellow box, cyan capsule — distinct at-a-glance reads in compound
	// bodies. Wire thickness 1.5 picks up clearly without overpowering the
	// engine's stock visualizers (capsule = green at 1.0, etc.).
	const FLinearColor BoxColor     = FLinearColor(1.0f, 0.85f, 0.0f);
	const FLinearColor CapsuleColor = FLinearColor(0.0f, 0.85f, 1.0f);
	const float Thickness = 1.5f;

	for (const FSeinExtentsShape& Shape : ExtComp->Data.Shapes)
	{
		// Pose: actor transform → rotate LocalOffset → world pos; actor yaw
		// + per-shape YawOffset → shape orientation.
		const FVector LocalOffset(
			Shape.LocalOffset.X.ToFloat(),
			Shape.LocalOffset.Y.ToFloat(),
			Shape.LocalOffset.Z.ToFloat());
		const FVector WorldOffset = ActorQuat.RotateVector(LocalOffset);
		const FVector ShapeBase = ActorPos + WorldOffset;

		const float YawOffsetDeg = Shape.YawOffsetDegrees.ToFloat();
		const FQuat YawOffsetQuat(FVector::UpVector, FMath::DegreesToRadians(YawOffsetDeg));
		const FQuat ShapeQuat = ActorQuat * YawOffsetQuat;

		const FVector AxisX = ShapeQuat.GetForwardVector();
		const FVector AxisY = ShapeQuat.GetRightVector();
		const FVector AxisZ = ShapeQuat.GetUpVector();

		const float Height = FMath::Max(0.0f, Shape.Height.ToFloat());
		// All shapes anchor at the entity foot — Z extends UPWARD by Height.
		// Wireframe center is therefore raised by Height/2 along local Up so
		// the wire spans foot plane → top.
		const FVector ShapeCenter = ShapeBase + AxisZ * (Height * 0.5f);

		switch (Shape.Shape)
		{
		case ESeinExtentsShape::Box:
		{
			const FVector HalfExtents(
				FMath::Max(0.0f, Shape.HalfExtentX.ToFloat()),
				FMath::Max(0.0f, Shape.HalfExtentY.ToFloat()),
				Height * 0.5f);
			DrawOrientedWireBox(
				PDI,
				ShapeCenter,
				AxisX, AxisY, AxisZ,
				HalfExtents,
				BoxColor.ToFColor(true),
				SDPG_World,
				Thickness);
			break;
		}

		case ESeinExtentsShape::Capsule:
		{
			const float Radius = FMath::Max(0.0f, Shape.Radius.ToFloat());
			// UE's DrawWireCapsule HalfHeight is total cap-to-cap half-distance
			// (includes hemispheres). Height < 2×Radius would draw a degenerate
			// pinched capsule — clamp HalfHeight to at least Radius so the
			// caps don't intersect on tiny / wide entities.
			const float HalfHeight = FMath::Max(Height * 0.5f, Radius);
			DrawWireCapsule(
				PDI,
				ShapeCenter,
				AxisX, AxisY, AxisZ,
				CapsuleColor.ToFColor(true),
				Radius,
				HalfHeight,
				/*NumSides*/ 16,
				SDPG_World,
				Thickness);
			break;
		}
		}
	}
}
