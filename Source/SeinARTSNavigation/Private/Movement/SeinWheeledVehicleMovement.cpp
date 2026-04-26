/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.cpp
 */

#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinWheeled, Log, All);

USeinWheeledVehicleMovement::USeinWheeledVehicleMovement()
	: Wheelbase(FFixedPoint::FromInt(220))
	// Defaults expressed as rational / DegToRad expressions instead of
	// FromFloat literals so the CDO ctor never runs a runtime float→fixed
	// conversion. Bit-identical across PC / ARM / mobile / console.
	, MaxSteerAngle(FFixedPoint::Pi / FFixedPoint::FromInt(3)) // 60° (π/3) in radians
	, SteerResponse(FFixedPoint::FromInt(3))
	, LookAheadDistance(FFixedPoint::FromInt(300))
	// 0 = pure kinematic braking (recommended). Set non-zero per-unit to
	// force an earlier visible slowdown if the physics curve feels too late.
	, ArrivalSlowdownDistance(FFixedPoint::Zero)
	, TurnSpeedFloor(FFixedPoint::Half)
	, CurrentSteer(FFixedPoint::Zero)
{
}

void USeinWheeledVehicleMovement::OnMoveBegin(const FSeinLocomotionContext& Ctx)
{
	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementData& MoveData = Ctx.MoveData;
	const FSeinPath& Path = Ctx.Path;

	// Reset only the per-action transient (wheels self-center). MoveData.CurrentSpeed
	// is intentionally preserved so a vehicle reordered mid-drive doesn't
	// instant-stop and re-accelerate from rest.
	CurrentSteer = FFixedPoint::Zero;

	// Latch the reverse decision against the FINAL goal — once we commit to
	// reversing, we follow through. Repath later may re-evaluate, but
	// per-tick polling without hysteresis would oscillate at the threshold.
	const int32 N = Path.Waypoints.Num();
	if (N > 0)
	{
		bIsReversing = ShouldAutoReverse(
			Entity.Transform.GetLocation(),
			Entity.Transform.Rotation,
			Path.Waypoints[N - 1],
			MoveData);
	}
	else
	{
		bIsReversing = false;
	}
}

bool USeinWheeledVehicleMovement::Tick(const FSeinLocomotionContext& Ctx)
{
	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementData& MoveData = Ctx.MoveData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	// Pull persisted speed off the component — preserves momentum across
	// reorders. Written back at end of tick.
	FFixedPoint CurrentSpeed = MoveData.CurrentSpeed;

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// Arrival — two ways to fire:
	//   (a) Within AcceptanceRadius (normal case)
	//   (b) Overshoot: close, slow, AND heading away — covers the
	//       "death spiral" where turn radius can't hit a tight goal.
	// Neither snaps the unit's position; stopping inside the acceptance
	// envelope is the contract.
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;

		// Vicinity radius for overshoot detection: 2× AcceptanceRadius (linear).
		// Tight enough that a unit far from the goal during a U-turn maneuver
		// won't trigger; loose enough that a unit orbiting the goal at slow
		// speed will. Speed gate: 1/3 MoveSpeed — well below normal cruise.
		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MoveData.MoveSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = IsOvershootArrival(
			AgentPos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);

		const bool bClusterArrival = IsClusterArrival(Ctx, FinalWp);

		if (bWithinAcceptance || bOvershoot || bClusterArrival)
		{
			UE_LOG(LogSeinWheeled, Verbose,
				TEXT("Wheeled arrival: within=%d overshoot=%d cluster=%d distSq=%.1f speed=%.2f"),
				bWithinAcceptance ? 1 : 0, bOvershoot ? 1 : 0, bClusterArrival ? 1 : 0,
				ToFinal.SizeSquared().ToFloat(), CurrentSpeed.ToFloat());
			MoveData.CurrentSpeed = FFixedPoint::Zero;
			CurrentSteer = FFixedPoint::Zero;
			return true;
		}
	}

	// Waypoint advance — same vehicle-friendly radius as tracked.
	const FFixedPoint OneStep = MoveData.MoveSpeed * DeltaTime;
	const FFixedPoint AdvanceRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
		? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
	const FFixedPoint AdvanceRadiusSq = AdvanceRadius * AdvanceRadius;
	while (CurrentWaypointIndex < N - 1)
	{
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - AgentPos;
		ToWp.Z = FFixedPoint::Zero;
		if (ToWp.SizeSquared() <= AdvanceRadiusSq) ++CurrentWaypointIndex;
		else break;
	}

	// Steering target on the polyline.
	const FFixedPoint LookAhead = (LookAheadDistance > FFixedPoint::Zero)
		? LookAheadDistance : FFixedPoint::FromInt(100);
	const FFixedVector LookAheadPoint = ResolveLookAheadPoint(AgentPos, Path, CurrentWaypointIndex, LookAhead);

	FFixedVector ToTarget = LookAheadPoint - AgentPos;
	ToTarget.Z = FFixedPoint::Zero;

	// Aim error → desired steer angle (clamped to MaxSteerAngle). When the
	// look-ahead is degenerate (sitting on the agent), zero out the steer
	// command so the wheels self-center instead of locking last frame's value.
	//
	// Reverse case: the desired chassis yaw flips by π (we want the BACK to
	// face the target, not the front), and the steer command itself inverts —
	// bicycle reverse-kinematics: when the chassis moves backward, a steered
	// front wheel arcs the rear in the OPPOSITE direction relative to forward
	// driving. So the same yaw-error-magnitude needs the opposite steer sign.
	FFixedPoint DesiredSteer = FFixedPoint::Zero;
	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
	if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = bIsReversing
			? SeinMath::Atan2(-ToTarget.Y, -ToTarget.X)
			: SeinMath::Atan2(ToTarget.Y, ToTarget.X);
		const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		DesiredSteer = ClampFP(YawErr, -MaxSteerAngle, MaxSteerAngle);
		if (bIsReversing) DesiredSteer = -DesiredSteer;

		// Avoidance steering: convert the world-space bias into a yaw delta
		// and add to the steer input. Scaled by speed ratio because the
		// bicycle model produces zero yaw rate at zero speed — a vehicle
		// crawling at 10% speed has only ~10% steering authority, so the
		// bias should taper there to avoid commanding steer the vehicle
		// can't actually use.
		if (MoveData.FootprintRadius > FFixedPoint::Zero && MoveData.AvoidanceWeight > FFixedPoint::Zero)
		{
			const FFixedVector DesiredVel(
				SeinMath::Cos(CurrentYaw) * MoveData.MoveSpeed,
				SeinMath::Sin(CurrentYaw) * MoveData.MoveSpeed,
				FFixedPoint::Zero);
			const FFixedVector AvoidBias = ComputeAvoidanceVector(Ctx, DesiredVel);
			if (AvoidBias.SizeSquared() > FFixedPoint::Epsilon)
			{
				const FFixedPoint AvoidYaw = SeinMath::Atan2(AvoidBias.Y, AvoidBias.X);
				const FFixedPoint AvoidErr = ShortestAngleDelta(CurrentYaw, AvoidYaw);
				// Speed ratio in [0.3, 1.0]: floor of 0.3 keeps SOME steering
				// authority near rest so a stopped vehicle nudged into a
				// neighbor still tries to steer rather than locking the
				// wheels useless.
				const FFixedPoint AbsSpeed = (MoveData.CurrentSpeed < FFixedPoint::Zero)
					? -MoveData.CurrentSpeed : MoveData.CurrentSpeed;
				FFixedPoint SpeedRatio = (MoveData.MoveSpeed > FFixedPoint::Epsilon)
					? (AbsSpeed / MoveData.MoveSpeed) : FFixedPoint::Zero;
				const FFixedPoint Floor = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
				if (SpeedRatio < Floor) SpeedRatio = Floor;
				if (SpeedRatio > FFixedPoint::One) SpeedRatio = FFixedPoint::One;
				// Bias-strength ratio: atan2 above stripped |AvoidBias| down
				// to direction-only — without re-applying the magnitude as a
				// scalar, AvoidanceWeight becomes invisible to wheeled and
				// even a 0.25× configured weight commands a FULL turn toward
				// the avoid direction (long roundabout arcs: vehicle wheels
				// far away from a neighbor then has to correct all the way
				// back). |AvoidBias|/MoveSpeed is the natural intent ratio
				// since ComputeAvoidanceVector emits magnitudes in MoveSpeed
				// units; clamp to ≤1 so dense scenarios where summed bias
				// exceeds MoveSpeed cap at full-strength rather than
				// over-saturating the steer command.
				FFixedPoint StrengthRatio = (MoveData.MoveSpeed > FFixedPoint::Epsilon)
					? AvoidBias.Size() / MoveData.MoveSpeed : FFixedPoint::Zero;
				if (StrengthRatio > FFixedPoint::One) StrengthRatio = FFixedPoint::One;
				const FFixedPoint AvoidContribution = AvoidErr * SpeedRatio * StrengthRatio;
				DesiredSteer = ClampFP(DesiredSteer + AvoidContribution, -MaxSteerAngle, MaxSteerAngle);
			}
		}
	}

	// Smooth the steering toward desired (FInterpTo-equivalent: exponential
	// approach with a per-tick fraction = clamp(SteerResponse * dt, 0, 1)).
	{
		FFixedPoint Alpha = SteerResponse * DeltaTime;
		if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
		if (Alpha > FFixedPoint::One)  Alpha = FFixedPoint::One;
		CurrentSteer = CurrentSteer + (DesiredSteer - CurrentSteer) * Alpha;
	}

	// Bicycle yaw rate: ω = (v / L) · tan(δ). Speed-dependent — stationary
	// vehicles cannot pivot, which is the defining feature of wheeled. The
	// outer ClampFP by MoveData.TurnRate is a safety cap for high-speed
	// extreme-steer combos that would otherwise spin faster than the
	// designer-allowed rotation rate (e.g., bug guard on data inputs).
	FFixedPoint YawStep = FFixedPoint::Zero;
	FFixedPoint YawRateForLog = FFixedPoint::Zero;
	FFixedPoint TanSteerForLog = FFixedPoint::Zero;
	if (Wheelbase > FFixedPoint::One)
	{
		const FFixedPoint TanSteer = SeinMath::Tan(CurrentSteer);
		const FFixedPoint YawRate = (CurrentSpeed / Wheelbase) * TanSteer;
		const FFixedPoint MaxRate = MoveData.TurnRate;
		const FFixedPoint ClampedRate = ClampFP(YawRate, -MaxRate, MaxRate);
		YawStep = ClampedRate * DeltaTime;
		YawRateForLog = ClampedRate;
		TanSteerForLog = TanSteer;
	}
	const FFixedPoint NewYaw = CurrentYaw + YawStep;
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// Steering diagnostic — log every tick at Verbose. Use
	// `log LogSeinWheeled Verbose` to capture; defaults off in shipping.
	UE_LOG(LogSeinWheeled, Verbose,
		TEXT("Wheeled steer: pos=(%.1f,%.1f) yaw=%.4f desiredSteer=%.4f currSteer=%.4f tan=%.4f speed=%.2f yawRate=%.4f yawStep=%.5f newYaw=%.4f"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(),
		DesiredSteer.ToFloat(), CurrentSteer.ToFloat(),
		TanSteerForLog.ToFloat(),
		CurrentSpeed.ToFloat(),
		YawRateForLog.ToFloat(), YawStep.ToFloat(),
		NewYaw.ToFloat());

	// Throttle scaling. Wheeled does NOT gate on alignment (unlike tracked) —
	// it always tries to accelerate; the bicycle model handles arc geometry.
	// Optional sharp-turn slowdown via TurnSpeedFloor: throttle = lerp(1,
	// TurnSpeedFloor, (|steer|/MaxSteer)²) — quadratic so gentle steering is
	// barely slowed and only hard-lock corners brake meaningfully.
	FFixedPoint TurnScale = FFixedPoint::One;
	if (TurnSpeedFloor < FFixedPoint::One && MaxSteerAngle > FFixedPoint::Epsilon)
	{
		const FFixedPoint AbsSteer = (CurrentSteer < FFixedPoint::Zero) ? -CurrentSteer : CurrentSteer;
		FFixedPoint T = AbsSteer / MaxSteerAngle;
		if (T > FFixedPoint::One) T = FFixedPoint::One;
		const FFixedPoint TSq = T * T;
		TurnScale = FFixedPoint::One - (FFixedPoint::One - TurnSpeedFloor) * TSq;
	}

	// Kinematic arrival braking — see USeinLocomotion::KinematicArrivalSpeedCap.
	// Optional designer floor (`ArrivalSlowdownDistance > 0`) layers a linear
	// cap inside that distance, applied only when stricter than the physics
	// curve.
	FFixedPoint MaxArrivalSpeed;
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();

		MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, MoveData.Deceleration);

		if (ArrivalSlowdownDistance > FFixedPoint::Zero && DistFinal < ArrivalSlowdownDistance)
		{
			const FFixedPoint LinearCap = MoveData.MoveSpeed * (DistFinal / ArrivalSlowdownDistance);
			if (LinearCap < MaxArrivalSpeed) MaxArrivalSpeed = LinearCap;
		}
	}

	// Resolve target speed magnitude — forward uses MoveSpeed, reverse uses
	// the configured ReverseMaxSpeed (or MoveSpeed/2 fallback if 0). Apply
	// turn-scale + arrival cap on the magnitude, then sign-restore for
	// reverse so CurrentSpeed carries the direction.
	FFixedPoint TargetSpeedMag;
	if (bIsReversing)
	{
		TargetSpeedMag = (MoveData.ReverseMaxSpeed > FFixedPoint::Zero)
			? MoveData.ReverseMaxSpeed
			: MoveData.MoveSpeed * FFixedPoint::Half;
	}
	else
	{
		TargetSpeedMag = MoveData.MoveSpeed;
	}
	TargetSpeedMag = TargetSpeedMag * TurnScale;
	if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
	const FFixedPoint TargetSpeed = bIsReversing ? -TargetSpeedMag : TargetSpeedMag;
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MoveData.Acceleration, MoveData.Deceleration, DeltaTime);

	// Translate along the post-rotation forward.
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// Hard nav-collision resolve. Wheeled steering can over-commit to a turn
	// past the path's safe corridor; this floors the unit against blocked
	// cells so a wide turn radius can't carry it through a wall.
	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav);

	if (Nav)
	{
		FFixedPoint GroundZ;
		if (Nav->GetGroundHeightAt(NewPos, GroundZ))
		{
			NewPos.Z = GroundZ;
		}
	}

	Entity.Transform.SetLocation(NewPos);
	// Persist speed for next tick / next move action.
	MoveData.CurrentSpeed = CurrentSpeed;
	return false;
}
