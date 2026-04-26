/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinMovementData.h
 * @brief:   Sim-side movement component data. Deterministic, stored in ECS
 *           storage; the USeinMovementComponent UActorComponent carries this
 *           struct as its sim payload.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPath.h"
#include "Components/SeinComponent.h"
#include "Components/SeinExtentsData.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMovementData.generated.h"

/** When the move-to action recomputes its path during execution. The original
 *  path is computed once at move-start; long moves can drift off-path due to
 *  vehicle turn dynamics or world changes (gates, destroyed buildings,
 *  obstacles appearing). Repathing periodically keeps the unit honest. */
UENUM(BlueprintType)
enum class ESeinRepathMode : uint8
{
	/** Re-run FindPath every `RepathInterval` seconds from current position.
	 *  Robust + simple; cost is one A* search per unit per interval. */
	Interval        UMETA(DisplayName = "Interval"),

	/** Repath only when the unit has demonstrably drifted off the planned
	 *  polyline (cheaper, more reactive). Implementation is deferred —
	 *  selecting this currently behaves as no-repath until the off-path
	 *  detector is wired up. */
	OffPathOnly     UMETA(DisplayName = "Off-Path Only")
};

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMovementData : public FSeinComponent
{
	GENERATED_BODY()

	/** Movement speed in units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint MoveSpeed;

	/** Target location to move toward */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedVector TargetLocation;

	/** Whether entity currently has a move target */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	bool bHasTarget;

	/** Acceleration rate (world units per second²) — how quickly current speed
	 *  ramps UP toward target. Used by vehicle locomotions for the smoothstep
	 *  speed model; infantry's seek-and-arrive ignores it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint Acceleration;

	/** Deceleration rate (world units per second²) — how quickly current speed
	 *  ramps DOWN toward target. Typically larger than Acceleration so vehicles
	 *  brake harder than they accelerate. Vehicle-only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint Deceleration;

	/** Turn rate in radians per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint TurnRate;

	/** Persistent signed forward speed (world units / second). Lives on the
	 *  component, not the per-action locomotion instance, so velocity carries
	 *  smoothly across new move orders — issuing a new MoveTo while in motion
	 *  preserves momentum instead of snapping to zero. Locomotions read this
	 *  on Tick entry and write back on exit. Final-arrival logic zeros it
	 *  (units come to rest at the destination); cancellation/preemption
	 *  intentionally leaves it set so the next order picks up where the
	 *  previous left off. Sign is reserved for future reverse handling. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Movement")
	FFixedPoint CurrentSpeed;

	/** How close (world units) the unit needs to be to its move destination
	 *  to count as "arrived." Properly a unit characteristic — wheeled
	 *  vehicles need bigger tolerance than infantry because turn radius
	 *  prevents tight precision arrivals. Move abilities can still pass an
	 *  override on the MoveTo action; if they pass <= 0, this default
	 *  is used. Default 50 = half a 100 cm cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement", meta = (ClampMin = "0.0"))
	FFixedPoint AcceptanceRadius;

	/** When the active move-to action re-runs FindPath. See `ESeinRepathMode`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Repath")
	ESeinRepathMode RepathMode;

	/** Seconds between periodic repaths (Interval mode only). Smaller values
	 *  react faster to world changes / drift but cost more A* searches per
	 *  unit. Hidden in the editor unless `RepathMode == Interval`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Repath",
		meta = (ClampMin = "0.05",
				EditCondition = "RepathMode == ESeinRepathMode::Interval", EditConditionHides))
	FFixedPoint RepathInterval;

	/** Whether this unit can drive in reverse. When true, vehicle locomotions
	 *  will auto-engage reverse for nearby destinations that are behind the
	 *  unit (the CoH "back up to a close target instead of a U-turn" feel),
	 *  and explicit reverse abilities — when wired up — will work. Off by
	 *  default; designers opt in per unit type (tanks/cars yes, infantry no). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Reverse")
	bool bCanReverse;

	/** Maximum speed (world units / second) when reversing. Vehicles
	 *  typically reverse slower than they drive forward. Set to 0 to use
	 *  half of MoveSpeed as a fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Reverse",
		meta = (ClampMin = "0.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseMaxSpeed;

	/** Auto-reverse engages when `forward · normalize(toGoal)` is at or below
	 *  this dot threshold AND distance to goal is within
	 *  `ReverseEngageDistance`. Default -0.5 ≈ 120° behind; lower (more
	 *  negative) is stricter. Range [-1, +1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Reverse",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseEngageDotThreshold;

	/** Auto-reverse only engages within this distance of the destination. A
	 *  far-away rear target would U-turn forward instead — reversing all the
	 *  way looks silly. Default 5m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Reverse",
		meta = (ClampMin = "0.0", EditCondition = "bCanReverse"))
	FFixedPoint ReverseEngageDistance;

	/** Collision footprint radius (world units). Drives both penetration
	 *  resolution (units never overlap their footprints) and avoidance
	 *  perception. 0 = unit is "intangible" — opts out of all avoidance.
	 *  Default 30 (infantry); vehicles should bump it per BP — wheeled
	 *  ~100, tracked ~150. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance", meta = (ClampMin = "0.0"))
	FFixedPoint FootprintRadius;

	/** How far ahead the unit looks for neighbors when computing avoidance
	 *  steering. 0 = auto-derive as `FootprintRadius × 4`. Larger values
	 *  produce smoother early-arc avoidance but cost more spatial-hash
	 *  query work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance", meta = (ClampMin = "0.0"))
	FFixedPoint PerceptionRadius;

	/** Multiplier on the avoidance steering force (0 disables anticipation
	 *  entirely while still participating in penetration resolution).
	 *  Default 1.0; raise for more cautious units, lower for "pushy" ones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance", meta = (ClampMin = "0.0"))
	FFixedPoint AvoidanceWeight;

	/** Multiplier on the static-wall repulsion force (sampled forward-arc
	 *  against the nav's `IsPassable`). Slightly stronger than agent
	 *  avoidance by default — running into a building is worse than
	 *  brushing a neighbor. 0 disables wall avoidance entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement|Avoidance", meta = (ClampMin = "0.0"))
	FFixedPoint WallAvoidanceWeight;

	/** Smoothed avoidance bias from the previous tick. Each call to
	 *  `USeinLocomotion::ComputeAvoidanceVector` lerps the freshly-computed
	 *  bias toward this value, then writes the smoothed result back. Provides
	 *  temporal coherence — without it, bias flips sign every couple ticks
	 *  as units pass each other (closest-approach geometry inverts), causing
	 *  visible twitch. Persists across move orders so a unit with momentum
	 *  carries its avoidance state into the next destination. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Avoidance")
	FFixedVector LastAvoidBias;

	/** Locomotion class — how the entity advances along a nav path (seek+arrive,
	 *  turn-in-place tracked, turn-radius wheeled, etc.). Soft class path (not
	 *  TSubclassOf) because the concrete USeinLocomotion subclasses live in
	 *  SeinARTSNavigation, which depends on this module; a direct TSubclassOf
	 *  would flip the dep and create a cycle. Resolved to a UClass* at
	 *  action-init time via TryLoadClass.
	 *
	 *  Null / invalid defaults to USeinBasicMovement (simple seek + arrive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (DisplayName = "Locomotion Class",
				MetaClass = "/Script/SeinARTSNavigation.SeinLocomotion"))
	FSoftClassPath LocomotionClass;

	/** Nav layer mask — which layer bits identify this agent for blocker
	 *  intersection. Pathing is gated by `(NavLayerMask & Blocker.
	 *  BlockedNavLayerMask) != 0` → blocked. Default 0x01 = bit 0 (Default
	 *  agent class).
	 *
	 *  "Amphibious skips water" pattern: water blocker authored with
	 *  `BlockedNavLayerMask = 0x01` (Default only); amphibious agent
	 *  authored with `NavLayerMask = 0x02` (bit 1 only — drops the Default
	 *  bit). Intersection 0x02 & 0x01 = 0 → amphibious unblocked.
	 *  Multi-class agents (e.g. amphibious vehicle that ALSO blocks like
	 *  a tank) author both bits: `NavLayerMask = 0x03`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSCoreEntity.ESeinNavLayerBit"))
	uint8 NavLayerMask = 0x01;

	FSeinMovementData()
		: MoveSpeed(FFixedPoint::FromInt(5))
		, TargetLocation(FFixedVector::ZeroVector)
		, bHasTarget(false)
		, Acceleration(FFixedPoint::FromInt(10))
		, Deceleration(FFixedPoint::FromInt(20))
		, TurnRate(FFixedPoint::FromInt(5))
		, CurrentSpeed(FFixedPoint::Zero)
		, AcceptanceRadius(FFixedPoint::FromInt(50))
		, RepathMode(ESeinRepathMode::Interval)
		// 0.25s = 4 repaths/sec. Aggressive for a starting default — cheap
		// to dial back per-unit if the cost matters. Expressed as ratio to
		// keep the CDO ctor free of any runtime float→fixed conversion.
		, RepathInterval(FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4))
		// Reverse is opt-in. Defaults configured for tanks/cars when enabled
		// (-0.5 dot ≈ 120° behind, 500cm range, 0 max speed = "use MoveSpeed/2").
		, bCanReverse(false)
		, ReverseMaxSpeed(FFixedPoint::Zero)
		, ReverseEngageDotThreshold(-FFixedPoint::Half)
		, ReverseEngageDistance(FFixedPoint::FromInt(500))
		// Avoidance defaults — OOTB-on so units avoid each other without
		// per-unit configuration. Set FootprintRadius back to 0 to opt out
		// (cursors, decoration, intangible markers). 30 = a reasonable
		// infantry footprint; vehicles should bump it (~100 wheeled, ~150
		// tracked) on their own BP.
		, FootprintRadius(FFixedPoint::FromInt(30))
		, PerceptionRadius(FFixedPoint::Zero) // 0 = auto = FootprintRadius × 4
		, AvoidanceWeight(FFixedPoint::One)
		// Wall repulsion runs slightly hotter than agent repulsion: walking
		// into a building is more costly than brushing a neighbor. 1.5 as a
		// 3:2 integer ratio so the CDO ctor is bit-identical cross-arch.
		, WallAvoidanceWeight(FFixedPoint::FromInt(3) / FFixedPoint::FromInt(2))
		, LastAvoidBias(FFixedVector::ZeroVector)
		, LocomotionClass(FSoftClassPath(TEXT("/Script/SeinARTSNavigation.SeinBasicMovement")))
		, NavLayerMask(0x01) // Default bit
	{}
};

FORCEINLINE uint32 GetTypeHash(const FSeinMovementData& Component)
{
	uint32 Hash = GetTypeHash(Component.MoveSpeed);
	Hash = HashCombine(Hash, GetTypeHash(Component.TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(Component.bHasTarget));
	Hash = HashCombine(Hash, GetTypeHash(Component.Acceleration));
	Hash = HashCombine(Hash, GetTypeHash(Component.Deceleration));
	Hash = HashCombine(Hash, GetTypeHash(Component.TurnRate));
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentSpeed));
	Hash = HashCombine(Hash, GetTypeHash(Component.AcceptanceRadius));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Component.RepathMode)));
	Hash = HashCombine(Hash, GetTypeHash(Component.RepathInterval));
	Hash = HashCombine(Hash, GetTypeHash(Component.bCanReverse));
	Hash = HashCombine(Hash, GetTypeHash(Component.ReverseMaxSpeed));
	Hash = HashCombine(Hash, GetTypeHash(Component.ReverseEngageDotThreshold));
	Hash = HashCombine(Hash, GetTypeHash(Component.ReverseEngageDistance));
	Hash = HashCombine(Hash, GetTypeHash(Component.FootprintRadius));
	Hash = HashCombine(Hash, GetTypeHash(Component.PerceptionRadius));
	Hash = HashCombine(Hash, GetTypeHash(Component.AvoidanceWeight));
	Hash = HashCombine(Hash, GetTypeHash(Component.WallAvoidanceWeight));
	Hash = HashCombine(Hash, GetTypeHash(Component.LastAvoidBias));
	Hash = HashCombine(Hash, GetTypeHash(Component.LocomotionClass));
	Hash = HashCombine(Hash, GetTypeHash(Component.NavLayerMask));
	return Hash;
}
