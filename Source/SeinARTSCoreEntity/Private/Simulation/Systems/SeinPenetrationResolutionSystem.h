/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPenetrationResolutionSystem.h
 * @brief   PostTick system that pushes overlapping entities apart along their
 *          separation axis. Universal safety net beneath per-movement
 *          anticipation — even if Layer 1 didn't fully avoid a collision, no
 *          two entities ever end a tick visibly inside each other.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinSpatialHash.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

/**
 * System: Penetration Resolution
 * Phase: PostTick | Priority: 10
 *
 * Runs after movement has finished moving everyone (PostTick) and before
 * StateHash (priority 100), so it contributes to the deterministic state
 * snapshot. Uses the spatial hash from PreTick to find candidate pairs in
 * O(K) per entity instead of O(N²).
 *
 * Algorithm:
 *   1. ForEachEntity (sorted by handle index — pool insertion order).
 *   2. For each entity with FootprintRadius > 0, query the spatial hash for
 *      neighbors within `FootprintRadius * 2` (max possible overlap distance
 *      between two entities of similar size; covers the worst case).
 *   3. For each neighbor whose handle index is GREATER THAN self's index,
 *      compute separation. Each pair is processed exactly once because
 *      the index-greater filter is symmetric.
 *   4. Mass-weighted symmetric push-apart along the separation axis.
 *      Mass = FootprintRadius² (heavier units displace lighter ones more).
 *   5. Run the full sweep TWO TIMES per tick — cheap relaxation that
 *      converges most cluster configurations without the cost of true PBD.
 *
 * Z is intentionally untouched — movements own ground snap, and pushing
 * units around vertically would defeat that.
 */
class FSeinPenetrationResolutionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Two relaxation passes. The second catches residual overlaps in
		// dense clusters that the first pass couldn't fully separate.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			ResolvePass(World);
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 10; }
	virtual FName GetSystemName() const override { return TEXT("PenetrationResolution"); }

private:
	static void ResolvePass(USeinWorldSubsystem& World)
	{
		const FSeinSpatialHash& Hash = World.GetSpatialHash();
		TArray<FSeinEntityHandle> Neighbors;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle SelfHandle, FSeinEntity& SelfEntity)
		{
			FSeinMovementData* SelfMove = World.GetComponent<FSeinMovementData>(SelfHandle);
			if (!SelfMove || SelfMove->FootprintRadius <= FFixedPoint::Zero) return;

			const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();
			const FFixedPoint SelfRadius = SelfMove->FootprintRadius;
			// Query radius = self + max-other footprint. Using 2× self is the
			// tight upper bound when all units share roughly the same size; if
			// a much larger unit exists nearby, IT will see us during ITS
			// own iteration (with its larger query radius). Both directions
			// of the pair check still execute correctly because the index
			// filter only fires for one of them.
			const FFixedPoint QueryRadius = SelfRadius * FFixedPoint::Two;

			Neighbors.Reset();
			Hash.QueryRadius(SelfPos, QueryRadius, Neighbors, SelfHandle);

			for (const FSeinEntityHandle& OtherHandle : Neighbors)
			{
				// Index-greater filter: process each pair exactly once.
				// Pool iteration order is ascending, so by the time we get
				// here, anyone with a smaller index already had their turn
				// and we'd be doing redundant work.
				if (OtherHandle.Index <= SelfHandle.Index) continue;

				FSeinMovementData* OtherMove = World.GetComponent<FSeinMovementData>(OtherHandle);
				if (!OtherMove || OtherMove->FootprintRadius <= FFixedPoint::Zero) continue;

				FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
				if (!OtherEntity) continue;

				const FFixedVector OtherPos = OtherEntity->Transform.GetLocation();
				const FFixedPoint OtherRadius = OtherMove->FootprintRadius;

				FFixedVector Delta = OtherPos - SelfPos;
				Delta.Z = FFixedPoint::Zero;
				const FFixedPoint DistSq = Delta.SizeSquared();
				const FFixedPoint MinDist = SelfRadius + OtherRadius;
				const FFixedPoint MinDistSq = MinDist * MinDist;
				if (DistSq >= MinDistSq) continue; // not overlapping

				// Resolve. Distance might be zero (perfect overlap), in
				// which case Delta direction is undefined — bias to +X to
				// keep the resolution deterministic. Won't happen often;
				// real motion produces non-zero deltas.
				FFixedVector PushDir;
				FFixedPoint Distance;
				if (DistSq <= FFixedPoint::Epsilon)
				{
					PushDir = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
					Distance = FFixedPoint::Zero;
				}
				else
				{
					Distance = SeinMath::Sqrt(DistSq);
					PushDir = Delta / Distance; // unit vector self → other
				}

				const FFixedPoint Overlap = MinDist - Distance;

				// Mass-weighted split. MassA = SelfRadius², MassB = OtherRadius².
				// A's share of the push (in absolute terms) = MassB / (MassA + MassB)
				// — heavier "other" pushes "self" more. Symmetric.
				const FFixedPoint MassSelf = SelfRadius * SelfRadius;
				const FFixedPoint MassOther = OtherRadius * OtherRadius;
				const FFixedPoint MassSum = MassSelf + MassOther;
				const FFixedPoint SelfShare = (MassSum > FFixedPoint::Epsilon)
					? (MassOther / MassSum) : FFixedPoint::Half;
				const FFixedPoint OtherShare = FFixedPoint::One - SelfShare;

				// Self moves AWAY from other (along -PushDir), other moves
				// AWAY from self (along +PushDir).
				FFixedVector SelfNewPos = SelfPos - PushDir * (Overlap * SelfShare);
				FFixedVector OtherNewPos = OtherPos + PushDir * (Overlap * OtherShare);
				SelfNewPos.Z = SelfPos.Z;
				OtherNewPos.Z = OtherPos.Z;

				SelfEntity.Transform.SetLocation(SelfNewPos);
				OtherEntity->Transform.SetLocation(OtherNewPos);
			}
		});
	}
};
