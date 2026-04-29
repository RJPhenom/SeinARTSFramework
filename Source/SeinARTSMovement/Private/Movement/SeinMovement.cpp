/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovement.cpp
 */

#include "Movement/SeinMovement.h"
#include "Math/MathLib.h"
#include "SeinARTSMovementModule.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Simulation/SeinSpatialHash.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#endif

// Avoidance diagnostics. Off by default. Flip on at runtime with
// `log LogSeinAvoidance Verbose` to trace why a unit isn't avoiding (e.g.
// FootprintRadius=0, no neighbors found, bias too small to bend the path).
DEFINE_LOG_CATEGORY_STATIC(LogSeinAvoidance, Log, All);

FFixedPoint USeinMovement::ShortestAngleDelta(FFixedPoint From, FFixedPoint To)
{
	FFixedPoint Delta = To - From;
	// Wrap to [-π, π]. At most two iterations needed if inputs are already in
	// [-π, π] (typical case); guard with a loop anyway for safety.
	while (Delta > FFixedPoint::Pi)    { Delta = Delta - FFixedPoint::TwoPi; }
	while (Delta < -FFixedPoint::Pi)   { Delta = Delta + FFixedPoint::TwoPi; }
	return Delta;
}

FFixedPoint USeinMovement::ClampFP(FFixedPoint Val, FFixedPoint Min, FFixedPoint Max)
{
	if (Val < Min) return Min;
	if (Val > Max) return Max;
	return Val;
}

FFixedQuaternion USeinMovement::YawOnly(FFixedPoint YawRadians)
{
	// Roll=0, Pitch=0, Yaw=input. Matches MakeFromEulers' (X=roll, Y=pitch,
	// Z=yaw) convention.
	return FFixedQuaternion::MakeFromEulers(FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, YawRadians));
}

FFixedPoint USeinMovement::YawFromRotation(const FFixedQuaternion& Rotation)
{
	// Extract via the forward vector rather than Eulers() — Eulers has branches
	// for gimbal singularities at ±90° pitch that add cost we don't need for
	// upright yaw-only rotations.
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	return SeinMath::Atan2(Forward.Y, Forward.X);
}

FFixedVector USeinMovement::ResolveLookAheadPoint(
	const FFixedVector& AgentPos,
	const FSeinPath& Path,
	int32 CurrentWaypointIndex,
	FFixedPoint LookAhead)
{
	const int32 N = Path.Waypoints.Num();
	if (N == 0) return AgentPos;
	if (CurrentWaypointIndex >= N) return Path.Waypoints[N - 1];
	if (CurrentWaypointIndex < 0) CurrentWaypointIndex = 0;

	// First segment runs from the agent itself to the current waypoint, then
	// each subsequent segment connects waypoints. Walking from the agent
	// keeps the carrot strictly ahead even when the unit is partway through
	// a segment. Planar (XY) measurement only — Z drift on slopes shouldn't
	// shorten the look-ahead.
	FFixedVector SegStart = AgentPos;
	FFixedVector SegEnd = Path.Waypoints[CurrentWaypointIndex];
	int32 NextIdx = CurrentWaypointIndex;

	FFixedPoint Remaining = (LookAhead < FFixedPoint::Zero) ? FFixedPoint::Zero : LookAhead;

	while (true)
	{
		FFixedVector Seg = SegEnd - SegStart;
		Seg.Z = FFixedPoint::Zero;
		const FFixedPoint SegLen = Seg.Size();

		if (Remaining <= SegLen)
		{
			// Look-ahead point lies within this segment. Degenerate
			// zero-length segments (duplicate waypoints) fall through to the
			// "consume + advance" branch below since SegLen ≈ 0 < Remaining.
			if (SegLen > FFixedPoint::Epsilon)
			{
				const FFixedVector Dir = FFixedVector::GetSafeNormal(Seg);
				FFixedVector Out = SegStart + Dir * Remaining;
				Out.Z = SegEnd.Z; // adopt the segment-end Z; ground-snap is later
				return Out;
			}
			// Fall through: zero-length segment with Remaining ≈ 0 means we
			// just sit at SegEnd.
			return SegEnd;
		}

		// Consume this segment and advance.
		Remaining = Remaining - SegLen;
		++NextIdx;
		if (NextIdx >= N)
		{
			// Walked off the end — clamp to terminal waypoint.
			return Path.Waypoints[N - 1];
		}

		SegStart = SegEnd;
		SegEnd = Path.Waypoints[NextIdx];
	}
}

FFixedPoint USeinMovement::StepSpeedToward(
	FFixedPoint Current, FFixedPoint Target,
	FFixedPoint Accel, FFixedPoint Decel, FFixedPoint Dt)
{
	// Choose accel vs decel by whether |speed| is growing or shrinking. Sign
	// flips (e.g. forward → reverse transition) count as decel since the
	// magnitude must first cross zero.
	const FFixedPoint AbsCur = (Current < FFixedPoint::Zero) ? -Current : Current;
	const FFixedPoint AbsTgt = (Target  < FFixedPoint::Zero) ? -Target  : Target;
	const bool bSignFlip = (Current * Target) < FFixedPoint::Zero;
	const FFixedPoint Rate = (bSignFlip || AbsTgt < AbsCur) ? Decel : Accel;
	const FFixedPoint MaxStep = Rate * Dt;

	const FFixedPoint Delta = Target - Current;
	if (Delta > MaxStep)  return Current + MaxStep;
	if (Delta < -MaxStep) return Current - MaxStep;
	return Target;
}

FFixedPoint USeinMovement::KinematicArrivalSpeedCap(
	FFixedPoint DistToFinal, FFixedPoint Deceleration)
{
	// No decel = no kinematic cap. Return a value larger than any reasonable
	// MoveSpeed so the cap effectively "doesn't apply" at the call site.
	if (Deceleration <= FFixedPoint::Zero) return FFixedPoint::FromInt(1000000);
	if (DistToFinal <= FFixedPoint::Zero) return FFixedPoint::Zero;

	const FFixedPoint TwoAD = FFixedPoint::Two * Deceleration * DistToFinal;
	if (TwoAD <= FFixedPoint::Zero) return FFixedPoint::Zero;
	return SeinMath::Sqrt(TwoAD);
}

FFixedVector USeinMovement::ResolveNavCollision(
	const FFixedVector& OldPos,
	const FFixedVector& NewPos,
	USeinNavigation* Nav)
{
	if (!Nav) return NewPos;

	// Fast path — full step lands on a passable cell. Z is preserved as-is;
	// ground-snap happens after this in the movement's own pass.
	if (Nav->IsPassable(NewPos)) return NewPos;

	// X-axis-only slide. Useful when the agent is moving diagonally into a
	// north-south wall — the X component clips, the Y component still slides.
	const FFixedVector XOnly(NewPos.X, OldPos.Y, NewPos.Z);
	if (Nav->IsPassable(XOnly)) return XOnly;

	// Y-axis-only slide. Mirror case for east-west walls.
	const FFixedVector YOnly(OldPos.X, NewPos.Y, NewPos.Z);
	if (Nav->IsPassable(YOnly)) return YOnly;

	// Cornered / dead-ended. Hold position. Movement's existing logic
	// (turn-rate, avoidance bias, repath) drives the next attempt.
	return OldPos;
}

bool USeinMovement::IsOvershootArrival(
	const FFixedVector& AgentPos,
	const FFixedVector& FinalWp,
	const FFixedQuaternion& Rotation,
	FFixedPoint CurrentSpeed,
	FFixedPoint VicinityRadiusSq,
	FFixedPoint MaxSpeedForOvershoot)
{
	FFixedVector ToFinal = FinalWp - AgentPos;
	ToFinal.Z = FFixedPoint::Zero;
	if (ToFinal.SizeSquared() > VicinityRadiusSq) return false;

	const FFixedPoint AbsSpeed = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;
	if (AbsSpeed > MaxSpeedForOvershoot) return false;

	// "Heading away" — forward · toFinal < 0. ToFinal is non-zero here only
	// if the unit is offset from FinalWp; degenerate-zero falls through to
	// the dot returning 0 and not triggering.
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint Dot = Forward.X * ToFinal.X + Forward.Y * ToFinal.Y;
	return Dot < FFixedPoint::Zero;
}

bool USeinMovement::ShouldAutoReverse(
	const FFixedVector& AgentPos,
	const FFixedQuaternion& Rotation,
	const FFixedVector& FinalGoal,
	const FSeinMovementData& MoveData)
{
	if (!MoveData.bCanReverse) return false;

	FFixedVector ToGoal = FinalGoal - AgentPos;
	ToGoal.Z = FFixedPoint::Zero;
	const FFixedPoint DistSq = ToGoal.SizeSquared();
	const FFixedPoint MaxDistSq = MoveData.ReverseEngageDistance * MoveData.ReverseEngageDistance;
	if (DistSq > MaxDistSq) return false;
	if (DistSq <= FFixedPoint::Epsilon) return false; // already on goal

	// Compare normalized dot against threshold. Threshold is typically
	// negative (target is behind) — using <= so the boundary case engages.
	const FFixedVector ToGoalN = FFixedVector::GetSafeNormal(ToGoal);
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint Dot = Forward.X * ToGoalN.X + Forward.Y * ToGoalN.Y;
	return Dot <= MoveData.ReverseEngageDotThreshold;
}

bool USeinMovement::IsClusterArrival(
	const FSeinMovementContext& Ctx,
	const FFixedVector& FinalGoal)
{
	const FSeinMovementData& MoveData = Ctx.MoveData;

	// No avoidance / no spatial-hash access => can't query neighbors => can't
	// cluster-arrive. Falls through to plain acceptance-radius / overshoot.
	if (MoveData.FootprintRadius <= FFixedPoint::Zero) return false;
	if (!Ctx.World) return false;

	const FFixedVector AgentPos = Ctx.Entity.Transform.GetLocation();

	// Vicinity of the destination — max of (3× acceptance radius, 2× footprint).
	// Compared as squared values so we avoid a sqrt:
	//   acceptance-side: (3 × R)² = 9 × R² = 9 × AcceptanceRadiusSq
	//   footprint-side:  (2 × F)² = 4 × F²
	// Scaling with footprint gives big vehicles a proportionally wider
	// "good enough to call it done" zone, but 2× keeps it modest — 4× was
	// huge for a tank-sized footprint and let cluster arrival trigger from
	// way too far out.
	FFixedVector ToFinal = FinalGoal - AgentPos;
	ToFinal.Z = FFixedPoint::Zero;
	const FFixedPoint VicinityFromAcceptanceSq = Ctx.AcceptanceRadiusSq * FFixedPoint::FromInt(9);
	const FFixedPoint FootprintSq = MoveData.FootprintRadius * MoveData.FootprintRadius;
	const FFixedPoint VicinityFromFootprintSq = FootprintSq * FFixedPoint::FromInt(4);
	const FFixedPoint VicinityRadiusSq = (VicinityFromAcceptanceSq > VicinityFromFootprintSq)
		? VicinityFromAcceptanceSq : VicinityFromFootprintSq;
	const bool bInVicinity = (ToFinal.SizeSquared() <= VicinityRadiusSq);
	if (!bInVicinity)
	{
		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("Cluster[%s]: NOT in vicinity — distToFinal²=%.0f vicinity²=%.0f (vic≈%.1f)"),
			*Ctx.SelfHandle.ToString(),
			ToFinal.SizeSquared().ToFloat(),
			VicinityRadiusSq.ToFloat(),
			SeinMath::Sqrt(VicinityRadiusSq).ToFloat());
		return false;
	}

	// Look for a stationary neighbor that's "close enough" to count as a
	// parked anchor. Distance check uses combined-radius (self + other),
	// not just self — a vehicle approaching a building needs to detect
	// that building from outside its own footprint, not just inside.
	// Query radius is generous (self's footprint × 4) so the spatial hash
	// returns building-sized neighbors at meaningful distance; the
	// per-neighbor combined-radius check is the actual gate.
	const FFixedPoint AtRestThreshold = FFixedPoint::FromInt(10);  // 10 cm/s
	const FFixedPoint QueryRadius = MoveData.FootprintRadius * FFixedPoint::FromInt(4);

	const FSeinSpatialHash& Hash = Ctx.World->GetSpatialHash();
	TArray<FSeinEntityHandle> Neighbors;
	Hash.QueryRadius(AgentPos, QueryRadius, Neighbors, Ctx.SelfHandle);

	for (const FSeinEntityHandle& OtherHandle : Neighbors)
	{
		const FSeinEntity* OtherEntity = Ctx.World->GetEntityPool().Get(OtherHandle);
		if (!OtherEntity) continue;

		const FSeinMovementData* OtherMove = Ctx.World->GetComponent<FSeinMovementData>(OtherHandle);
		if (!OtherMove) continue;

		const FFixedPoint OtherAbs = (OtherMove->CurrentSpeed < FFixedPoint::Zero)
			? -OtherMove->CurrentSpeed : OtherMove->CurrentSpeed;
		if (OtherAbs >= AtRestThreshold) continue; // still moving — not a parked anchor

		// Close-enough = within 1.5× combined-footprint. At 1.0× the units are
		// touching footprints; 1.5× gives a small buffer so cluster arrival
		// can fire just before penetration resolution would have to push.
		const FFixedPoint Combined = MoveData.FootprintRadius + OtherMove->FootprintRadius;
		const FFixedPoint CloseEnough = Combined * FFixedPoint::FromInt(15) / FFixedPoint::FromInt(10);
		const FFixedPoint CloseEnoughSq = CloseEnough * CloseEnough;

		FFixedVector ToOther = OtherEntity->Transform.GetLocation() - AgentPos;
		ToOther.Z = FFixedPoint::Zero;
		const FFixedPoint DistSq = ToOther.SizeSquared();
		if (DistSq <= CloseEnoughSq)
		{
			UE_LOG(LogSeinAvoidance, Verbose,
				TEXT("Cluster[%s]: ARRIVED — anchor=%s (combined=%.1f closeEnough=%.1f dist=%.1f)"),
				*Ctx.SelfHandle.ToString(),
				*OtherHandle.ToString(),
				Combined.ToFloat(),
				CloseEnough.ToFloat(),
				SeinMath::Sqrt(DistSq).ToFloat());
			return true;
		}
	}
	UE_LOG(LogSeinAvoidance, Verbose,
		TEXT("Cluster[%s]: in vicinity but NO parked anchor in range (queryRadius=%.1f, neighbors=%d)"),
		*Ctx.SelfHandle.ToString(),
		QueryRadius.ToFloat(),
		Neighbors.Num());
	return false;
}

FFixedVector USeinMovement::ComputeAvoidanceVector(
	const FSeinMovementContext& Ctx,
	const FFixedVector& DesiredVelocity,
	FFixedPoint LookAheadSeconds)
{
	// Non-const so we can write LastAvoidBias for temporal smoothing — the
	// reference member of FSeinMovementContext stays mutable through a
	// const Ctx (reference-member constness doesn't propagate from outer).
	FSeinMovementData& MoveData = Ctx.MoveData;

	// Opt-out: zero footprint = no participation, zero weight = anticipation
	// disabled (penetration resolution still runs).
	if (MoveData.FootprintRadius <= FFixedPoint::Zero)
	{
		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("Avoid[%s]: SKIP — FootprintRadius=0 (intangible)"),
			*Ctx.SelfHandle.ToString());
		return FFixedVector::ZeroVector;
	}
	if (MoveData.AvoidanceWeight <= FFixedPoint::Zero)
	{
		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("Avoid[%s]: SKIP — AvoidanceWeight=0 (anticipation off)"),
			*Ctx.SelfHandle.ToString());
		return FFixedVector::ZeroVector;
	}
	if (!Ctx.World)
	{
		UE_LOG(LogSeinAvoidance, Verbose,
			TEXT("Avoid[%s]: SKIP — no World context"),
			*Ctx.SelfHandle.ToString());
		return FFixedVector::ZeroVector;
	}

	// Perception radius — auto-derive as 4× footprint when unset, the
	// "look one cell-width-ish ahead" default.
	const FFixedPoint Perception = (MoveData.PerceptionRadius > FFixedPoint::Zero)
		? MoveData.PerceptionRadius
		: MoveData.FootprintRadius * FFixedPoint::FromInt(4);

	const FFixedVector SelfPos = Ctx.Entity.Transform.GetLocation();
	const FFixedQuaternion SelfRot = Ctx.Entity.Transform.Rotation;
	const FFixedVector SelfForward = SelfRot.RotateVector(FFixedVector::ForwardVector);

	// Self's signed velocity along forward — sign carries reverse direction.
	// We approximate the velocity vector as Forward × CurrentSpeed; that's
	// exact for non-strafing movements (everyone we currently ship).
	const FFixedPoint SelfSpeed = MoveData.CurrentSpeed;
	const FFixedVector SelfVel(
		SelfForward.X * SelfSpeed,
		SelfForward.Y * SelfSpeed,
		FFixedPoint::Zero);
	(void)DesiredVelocity; // reserved for future use (e.g. weight by intent vs. actual)

	const FSeinSpatialHash& Hash = Ctx.World->GetSpatialHash();
	TArray<FSeinEntityHandle> Neighbors;
	Hash.QueryRadius(SelfPos, Perception, Neighbors, Ctx.SelfHandle);

	FFixedVector Bias = FFixedVector::ZeroVector;

	for (const FSeinEntityHandle& OtherHandle : Neighbors)
	{
		const FSeinEntity* OtherEntity = Ctx.World->GetEntityPool().Get(OtherHandle);
		if (!OtherEntity) continue;

		const FSeinMovementData* OtherMove = Ctx.World->GetComponent<FSeinMovementData>(OtherHandle);
		if (!OtherMove) continue;

		const FFixedVector OtherPos = OtherEntity->Transform.GetLocation();
		FFixedVector OtherVel = FFixedVector::ZeroVector;
		const FFixedVector OtherForward = OtherEntity->Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		const FFixedPoint OtherSpeed = OtherMove->CurrentSpeed;
		OtherVel.X = OtherForward.X * OtherSpeed;
		OtherVel.Y = OtherForward.Y * OtherSpeed;

		// Relative kinematics, planar only.
		FFixedVector RelPos = OtherPos - SelfPos;
		RelPos.Z = FFixedPoint::Zero;
		FFixedVector RelVel = OtherVel - SelfVel;
		RelVel.Z = FFixedPoint::Zero;

		const FFixedPoint RelVelSq = RelVel.SizeSquared();
		const FFixedPoint CombinedRadius = MoveData.FootprintRadius + OtherMove->FootprintRadius;
		const FFixedPoint SafeRadiusSq = CombinedRadius * CombinedRadius;

		FFixedPoint TStar;          // closest-approach time clamped to [0, LookAhead]
		FFixedVector ClosestRel;    // RelPos at t = TStar

		if (RelVelSq <= FFixedPoint::Epsilon)
		{
			// Both stationary (or moving identically). Use current
			// separation — nothing to anticipate, but still repel from
			// neighbors already inside our perception bubble.
			TStar = FFixedPoint::Zero;
			ClosestRel = RelPos;
		}
		else
		{
			// t* that minimizes |RelPos + RelVel·t|² is -(RelPos·RelVel)/|RelVel|²
			const FFixedPoint Numer = RelPos.X * RelVel.X + RelPos.Y * RelVel.Y;
			TStar = -Numer / RelVelSq;
			if (TStar < FFixedPoint::Zero) TStar = FFixedPoint::Zero;
			if (TStar > LookAheadSeconds) TStar = LookAheadSeconds;
			ClosestRel.X = RelPos.X + RelVel.X * TStar;
			ClosestRel.Y = RelPos.Y + RelVel.Y * TStar;
			ClosestRel.Z = FFixedPoint::Zero;
		}

		const FFixedPoint ClosestDistSq = ClosestRel.SizeSquared();
		// Engagement margin scales with the threat type:
		//   - Moving: (3× CombinedRadius)² = 9× SafeRadiusSq. Long lead-time
		//     so units visibly curve apart well before contact.
		//   - Stationary: (1.5× CombinedRadius)² = 2.25× SafeRadiusSq.
		//     Stationary obstacles are predictable and don't need long lead;
		//     using the moving margin for big vehicles + buildings produces
		//     a 10m+ unreachable buffer that prevents park-adjacent
		//     destinations from completing.
		const FFixedPoint OtherAbsSpeedEarly = (OtherSpeed < FFixedPoint::Zero) ? -OtherSpeed : OtherSpeed;
		const FFixedPoint EarlyAtRest = FFixedPoint::FromInt(10);
		const FFixedPoint Margin = (OtherAbsSpeedEarly < EarlyAtRest)
			? SafeRadiusSq * (FFixedPoint::FromInt(225) / FFixedPoint::FromInt(100))   // 2.25× for stationary
			: SafeRadiusSq * FFixedPoint::FromInt(9);                                  // 9× for moving
		if (ClosestDistSq > Margin) continue;

		// Repulsion direction: away from the closest-approach offset (or
		// from current offset if degenerate). Magnitude scaled by urgency
		// (closer-in-time = stronger) and AvoidanceWeight.
		FFixedVector PushDir;
		if (ClosestDistSq <= FFixedPoint::Epsilon)
		{
			// Predicted dead-on collision — shove perpendicular to the
			// relative velocity so we sidestep rather than reverse-into.
			// If RelVel is also tiny, fall back to perpendicular of RelPos
			// (which we know is non-zero or we'd be inside the bucket
			// excluded by the SelfHandle filter).
			const FFixedVector Basis = (RelVelSq > FFixedPoint::Epsilon) ? RelVel : RelPos;
			PushDir.X = -Basis.Y;
			PushDir.Y = Basis.X;
			PushDir.Z = FFixedPoint::Zero;
			PushDir = FFixedVector::GetSafeNormal(PushDir);
		}
		else
		{
			PushDir = FFixedVector::GetSafeNormal(ClosestRel) * FFixedPoint::FromInt(-1);
		}

		// Urgency = (1 − tStar/LookAhead) clamped to [floor, 1]. Imminent
		// threats get full weight; distant-but-incoming threats keep at
		// least `Floor` so they still nudge the path. Without the floor,
		// far-out predictions tapered to ~0 and anticipation only kicked in
		// at touch — Layer 2 then yanked the unit, no visible curve-apart.
		FFixedPoint Urgency = FFixedPoint::One;
		if (LookAheadSeconds > FFixedPoint::Zero)
		{
			Urgency = FFixedPoint::One - (TStar / LookAheadSeconds);
			const FFixedPoint Floor = FFixedPoint::FromInt(4) / FFixedPoint::FromInt(10);
			if (Urgency < Floor) Urgency = Floor;
		}

		// At-rest neighbor scaling. Stationary neighbors (CurrentSpeed ≈ 0)
		// contribute at 0.3× — they still push us off their footprint, but
		// not so hard that we twitch wildly when passing them. This is the
		// "parked unit doesn't shove you when you walk past" rule. Penetration
		// resolution still enforces non-overlap, so this only softens the
		// anticipation signal, not the hard collision floor.
		const FFixedPoint OtherAbsSpeed = (OtherSpeed < FFixedPoint::Zero) ? -OtherSpeed : OtherSpeed;
		const FFixedPoint AtRestThreshold = FFixedPoint::FromInt(10);  // 10 cm/s
		FFixedPoint NeighborWeight = FFixedPoint::One;
		if (OtherAbsSpeed < AtRestThreshold)
		{
			NeighborWeight = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10); // 0.3
		}

		const FFixedPoint Magnitude = MoveData.MoveSpeed * MoveData.AvoidanceWeight * Urgency * NeighborWeight;
		Bias.X = Bias.X + PushDir.X * Magnitude;
		Bias.Y = Bias.Y + PushDir.Y * Magnitude;

		// Per-neighbor contribution diagnostic. Fires only when the
		// magnitude actually moves the bias (>1 unit) so steady-state
		// "I'm next to my buddy but no one's threatening" doesn't spam.
		// Reads: who's pushing me, how hard, and how soon (tStar) so the
		// timeline of an approach can be reconstructed from a verbose log.
		if (Magnitude.ToFloat() > 1.0f)
		{
			UE_LOG(LogSeinAvoidance, Verbose,
				TEXT("Avoid[%s]:   from %s atRest=%d urgency=%.2f weight=%.2f mag=%.1f tStar=%.2f closestDist=%.1f"),
				*Ctx.SelfHandle.ToString(),
				*OtherHandle.ToString(),
				(OtherAbsSpeed < AtRestThreshold) ? 1 : 0,
				Urgency.ToFloat(),
				NeighborWeight.ToFloat(),
				Magnitude.ToFloat(),
				TStar.ToFloat(),
				SeinMath::Sqrt(ClosestDistSq).ToFloat());
		}
	}

	// Wall avoidance: 5 forward-arc probe rays against the nav's IsPassable.
	// Geometry: a wide-but-short arc weighted toward straight-ahead. The far
	// straight ray catches walls we'd hit at speed; the close wide rays catch
	// walls we're skimming past (corner-grinding case). Distance falloff
	// gives close blockers the dominant push so corners deflect cleanly
	// without far-distant walls warping the trajectory.
	if (Ctx.Nav && MoveData.WallAvoidanceWeight > FFixedPoint::Zero)
	{
		const FFixedPoint SelfYaw = SeinMath::Atan2(SelfForward.Y, SelfForward.X);

		// Angles in radians (0, ±30°, ±60°). Expressed as integer ratios of π
		// so the table is bit-identical cross-arch.
		const FFixedPoint Pi6 = FFixedPoint::Pi / FFixedPoint::FromInt(6);   // 30°
		const FFixedPoint Pi3 = FFixedPoint::Pi / FFixedPoint::FromInt(3);   // 60°

		// Distance ratios as integer fractions of Perception. Mirroring 1.0 /
		// 0.7 / 0.4 with /10 quanta keeps the CDO determinism story clean.
		const FFixedPoint Seven10 = FFixedPoint::FromInt(7) / FFixedPoint::FromInt(10);
		const FFixedPoint Four10  = FFixedPoint::FromInt(4) / FFixedPoint::FromInt(10);

		struct FWallSample { FFixedPoint AngleOffset; FFixedPoint DistRatio; };
		const FWallSample Samples[5] = {
			{ FFixedPoint::Zero, FFixedPoint::One },
			{  Pi6, Seven10 },
			{ -Pi6, Seven10 },
			{  Pi3, Four10  },
			{ -Pi3, Four10  },
		};

		for (int32 i = 0; i < 5; ++i)
		{
			const FFixedPoint SampleYaw = SelfYaw + Samples[i].AngleOffset;
			const FFixedPoint SampleDist = Perception * Samples[i].DistRatio;
			if (SampleDist <= FFixedPoint::Epsilon) continue;

			FFixedVector SamplePos;
			SamplePos.X = SelfPos.X + SeinMath::Cos(SampleYaw) * SampleDist;
			SamplePos.Y = SelfPos.Y + SeinMath::Sin(SampleYaw) * SampleDist;
			SamplePos.Z = SelfPos.Z;

			if (Ctx.Nav->IsPassable(SamplePos)) continue;

			// Push along the LATERAL component of (Self - Sample) — i.e., the
			// wall-tangent direction, not the wall normal. Without projecting
			// out the forward-axis component, samples in the front arc push
			// the unit anti-parallel to its forward direction, stalling a
			// soldier who skims a wall edge (his squadmates walk past while
			// he gets caught on the corner). Projecting keeps the "slide
			// along wall" component and discards the "reverse into open
			// space" component. The 0° probe projects exactly to zero, by
			// construction — intentional: a head-on wall is a path-planner
			// problem, not something steering can fix by backing up.
			// Magnitude tapers naturally with probe angle (|PushDir| ≈
			// |sin θ| of original), so ±60° probes — which carry the most
			// side information about the wall — retain most of their
			// strength while ±30° probes contribute roughly half. Per-probe
			// urgency still tapers linearly with sample distance below.
			FFixedVector AwayFromWall = SelfPos - SamplePos;
			AwayFromWall.Z = FFixedPoint::Zero;
			if (AwayFromWall.SizeSquared() <= FFixedPoint::Epsilon) continue;
			const FFixedVector PushDirRaw = FFixedVector::GetSafeNormal(AwayFromWall);
			const FFixedPoint ForwardDot = PushDirRaw.X * SelfForward.X + PushDirRaw.Y * SelfForward.Y;
			const FFixedVector PushDir(
				PushDirRaw.X - ForwardDot * SelfForward.X,
				PushDirRaw.Y - ForwardDot * SelfForward.Y,
				FFixedPoint::Zero);
			if (PushDir.SizeSquared() <= FFixedPoint::Epsilon) continue;

			FFixedPoint WallUrgency = FFixedPoint::One - (Samples[i].DistRatio);
			if (WallUrgency < FFixedPoint::Zero) WallUrgency = FFixedPoint::Zero;
			// Floor at 0.4 — even the farthest sample contributes a meaningful
			// nudge when blocked; the 1−ratio model otherwise zeros the
			// straight-ahead ray (DistRatio=1.0).
			const FFixedPoint Floor = FFixedPoint::FromInt(4) / FFixedPoint::FromInt(10);
			if (WallUrgency < Floor) WallUrgency = Floor;

			const FFixedPoint Magnitude =
				MoveData.MoveSpeed * MoveData.WallAvoidanceWeight * WallUrgency;
			Bias.X = Bias.X + PushDir.X * Magnitude;
			Bias.Y = Bias.Y + PushDir.Y * Magnitude;
		}
	}

	// Temporal smoothing: lerp the freshly-computed Bias toward the previous
	// tick's smoothed value. Without this, bias direction whips around as
	// units pass each other (closest-approach geometry inverts each frame),
	// producing visible twitching. Alpha = 0.2 means each tick the smoothed
	// vector moves 20% of the way toward the new raw bias — time constant
	// ~5 ticks (≈165ms at 30Hz). The movement further damps via
	// turn-rate / StepSpeedToward, so this slightly-aggressive damping is
	// imperceptible at the unit level but produces a stable debug arrow.
	const FFixedPoint SmoothAlpha = FFixedPoint::FromInt(2) / FFixedPoint::FromInt(10);
	FFixedVector Smoothed;
	Smoothed.X = MoveData.LastAvoidBias.X + (Bias.X - MoveData.LastAvoidBias.X) * SmoothAlpha;
	Smoothed.Y = MoveData.LastAvoidBias.Y + (Bias.Y - MoveData.LastAvoidBias.Y) * SmoothAlpha;
	Smoothed.Z = FFixedPoint::Zero;

	// Deadzone: clamp tiny smoothed biases to zero. Below 5 world-units of
	// bias the movement's path-following dominates and any residual is
	// pure noise — no point feeding micro-jitter to the steering.
	const FFixedPoint DeadzoneSq = FFixedPoint::FromInt(25); // 5²
	if (Smoothed.SizeSquared() < DeadzoneSq)
	{
		Smoothed = FFixedVector::ZeroVector;
	}

	// Persist for next tick's smoothing. Survives across move orders so a
	// unit's avoidance state flows through reorders the same way CurrentSpeed
	// carries momentum.
	MoveData.LastAvoidBias = Smoothed;

	// Per-call summary. Fires every tick a movement calls this — verbose
	// only, so off in release. Useful for "why isn't unit X avoiding?" —
	// shows neighbor count, raw bias, and the smoothed value the movement
	// will actually consume.
	UE_LOG(LogSeinAvoidance, Verbose,
		TEXT("Avoid[%s]: footprint=%.1f perception=%.1f neighbors=%d raw=(%.2f,%.2f)|%.2f smoothed=(%.2f,%.2f)|%.2f"),
		*Ctx.SelfHandle.ToString(),
		MoveData.FootprintRadius.ToFloat(),
		Perception.ToFloat(),
		Neighbors.Num(),
		Bias.X.ToFloat(), Bias.Y.ToFloat(), Bias.Size().ToFloat(),
		Smoothed.X.ToFloat(), Smoothed.Y.ToFloat(), Smoothed.Size().ToFloat());

	// Replace the raw Bias with the smoothed one for downstream debug draw +
	// the function's return value. Movement subclasses always consume the
	// smoothed result.
	Bias = Smoothed;

#if UE_ENABLE_DEBUG_DRAWING
	// Per-tick steering vector draw, gated on the SeinSteeringVectors custom
	// show flag (toggled by `Sein.Nav.Show.SteeringVectors`). Two arrows:
	//   - Cyan = current velocity (Forward × CurrentSpeed).
	//   - Magenta = avoidance bias. 1:1 world-unit scale so its magnitude is
	//     directly comparable to MoveSpeed — easy to eyeball "is the bias
	//     strong enough to deflect this unit's path?"
	// Tiny cyan dot stays for "unit present but stationary" cases where the
	// velocity arrow has zero length.
	//
	// LifeTime = 0.05s (50ms): bridges sim-tick gaps without producing visible
	// stacking. ComputeAvoidanceVector runs at the SIM tick rate (default 30Hz
	// = 33ms tick interval) but the renderer can run at 60+Hz; a `LifeTime=0`
	// debug primitive lives one engine tick, so at 60fps render the arrows
	// strobe (drawn every ~2 frames). 50ms ≈ 1.5× the 30Hz sim interval — each
	// new sim tick replaces the previous draws cleanly with ~1 sim-tick of
	// overlap, and at higher sim rates the overlap shrinks but never breaks.
	// Note: any LifeTime > 0 routes UE through the persistent line batcher,
	// which is fine — it cleans up expired primitives automatically each
	// engine tick.
	if (UWorld* DebugWorld = Ctx.World ? Ctx.World->GetWorld() : nullptr)
	{
		if (UE::SeinARTSMovement::IsSteeringVectorsShowFlagOnForWorld(DebugWorld))
		{
			const float DrawLifetime = 0.05f;

			const FVector Origin(SelfPos.X.ToFloat(), SelfPos.Y.ToFloat(), SelfPos.Z.ToFloat() + 50.0f);
			DrawDebugPoint(DebugWorld, Origin, 6.0f, FColor::Cyan, false, DrawLifetime);

			// Footprint ring — yellow circle at ground level showing the
			// unit's collision radius. Drawn in the XY plane (YAxis = world X,
			// ZAxis = world Y) — the "ZAxis" arg is the circle plane's
			// normal-mapped basis, NOT world up.
			const float FootprintF = MoveData.FootprintRadius.ToFloat();
			if (FootprintF > 1.0f)
			{
				const FVector GroundCenter(SelfPos.X.ToFloat(), SelfPos.Y.ToFloat(), SelfPos.Z.ToFloat() + 5.0f);
				DrawDebugCircle(DebugWorld, GroundCenter, FootprintF, 32,
					FColor::Yellow, false, DrawLifetime, 0, 3.0f,
					FVector(1.0f, 0.0f, 0.0f),  // YAxis (in-plane basis)
					FVector(0.0f, 1.0f, 0.0f),  // ZAxis (in-plane basis)
					/*bDrawAxis*/ false);
			}

			// Velocity vector — Forward × CurrentSpeed, world-unit-per-second
			// at 1:1 scale. Sign of CurrentSpeed encodes reverse, so reversing
			// units render the arrow opposite their facing.
			const float SpeedF = SelfSpeed.ToFloat();
			if (FMath::Abs(SpeedF) > 1.0f)
			{
				const FVector VelEnd = Origin + FVector(
					SelfForward.X.ToFloat() * SpeedF,
					SelfForward.Y.ToFloat() * SpeedF,
					0.0f);
				DrawDebugDirectionalArrow(DebugWorld, Origin, VelEnd,
					64.0f, FColor::Cyan, false, DrawLifetime, 0, 7.0f);
			}

			// Avoidance bias arrow — 1:1 magnitude (MoveSpeed-strength bias
			// = one second of pure-avoidance travel).
			const float BiasMag = Bias.Size().ToFloat();
			if (BiasMag > 1.0f)
			{
				const FVector End = Origin + FVector(Bias.X.ToFloat(), Bias.Y.ToFloat(), 0.0f);
				DrawDebugDirectionalArrow(DebugWorld, Origin, End,
					64.0f, FColor::Magenta, false, DrawLifetime, 0, 7.0f);
			}
		}
	}
#endif

	return Bias;
}
