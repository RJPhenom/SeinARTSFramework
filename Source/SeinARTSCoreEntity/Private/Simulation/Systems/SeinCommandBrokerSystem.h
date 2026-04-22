/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerSystem.h
 * @brief   Tick system for CommandBroker entities (DESIGN §5).
 *          PostTick phase.
 *
 *          Per broker, per tick:
 *            1. Strip dead members; mark capability map dirty if the list shrinks.
 *            2. Update centroid from live member positions.
 *            3. If executing: poll members; when every member's ability has ended,
 *               clear the executing flag, pop the front order, dispatch the next.
 *            4. If not executing + queue non-empty: rebuild capability map if
 *               dirty + call resolver + issue per-member ActivateAbility internally.
 *            5. If no members and no pending orders: cull via DestroyEntity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinAbilityData.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Abilities/SeinAbility.h"

/** Ability-execution dispatch helper shared with ProcessCommands (inline first-order
 *  dispatch) so both paths go through one code path. */
namespace SeinCommandBrokerDispatch
{
	static void ActivateMemberAbility(USeinWorldSubsystem& World, const FSeinBrokerMemberDispatch& MD)
	{
		if (!World.GetEntityPool().IsValid(MD.Member)) return;
		FSeinAbilityData* AC = World.GetComponent<FSeinAbilityData>(MD.Member);
		if (!AC) return;
		USeinAbility* Ability = AC->FindAbilityByTag(MD.AbilityTag);
		if (!Ability) return;

		if (Ability->IsOnCooldown()) return;
		if (!Ability->CanActivate()) return;

		// Broker-dispatched activation skips cost/cooldown-refund gating — the
		// broker-level order represents the "one player click" that already went
		// through ProcessCommands validation; per-member cost duplication is not
		// well-defined at V1. Designers can add per-member cost via their ability
		// OnActivate logic if they need it.
		for (USeinAbility* Other : AC->AbilityInstances)
		{
			if (Other && Other->bIsActive &&
				Other->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
			{
				Other->CancelAbility();
			}
		}
		Ability->ActivateAbility(MD.TargetEntity, MD.TargetLocation);
		if (!Ability->bIsPassive)
		{
			AC->ActiveAbility = Ability;
		}
	}

	static void RebuildCapabilityMap(USeinWorldSubsystem& World, FSeinCommandBrokerData& Broker)
	{
		Broker.CapabilityMap.Reset();
		for (const FSeinEntityHandle& M : Broker.Members)
		{
			const FSeinAbilityData* AC = World.GetComponent<FSeinAbilityData>(M);
			if (!AC) continue;
			for (const USeinAbility* Ab : AC->AbilityInstances)
			{
				if (Ab && Ab->AbilityTag.IsValid())
				{
					Broker.CapabilityMap.FindOrAdd(Ab->AbilityTag).Members.AddUnique(M);
				}
			}
		}
		Broker.bCapabilityMapDirty = false;
	}

	/** Dispatch the front queued order via the broker's resolver. Returns true if
	 *  the order was dispatched (broker.bIsExecuting was flipped on). */
	static bool DispatchFrontOrder(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		FSeinCommandBrokerData& Broker)
	{
		if (Broker.OrderQueue.Num() == 0 || Broker.Members.Num() == 0) return false;
		if (!Broker.Resolver) return false;

		if (Broker.bCapabilityMapDirty)
		{
			RebuildCapabilityMap(World, Broker);
		}

		const FSeinBrokerQueuedOrder& Front = Broker.OrderQueue[0];

		FSeinBrokerOrderInput Input;
		Input.ContextTag = Front.ContextTag;
		Input.TargetEntity = Front.TargetEntity;
		Input.TargetLocation = Front.TargetLocation;
		Input.FormationEnd = Front.FormationEnd;

		const FSeinBrokerDispatchPlan Plan = Broker.Resolver->ResolveDispatch(&World, BrokerHandle, Input);

		Broker.CurrentOrderTag = Front.ContextTag;
		Broker.Anchor = Front.TargetLocation;
		Broker.bIsExecuting = true;

		for (const FSeinBrokerMemberDispatch& MD : Plan.MemberDispatches)
		{
			ActivateMemberAbility(World, MD);
		}
		return true;
	}
}

/**
 * System: CommandBroker
 * Phase: PostTick | Priority: 40 (before StateHashSystem)
 */
class FSeinCommandBrokerSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		TArray<FSeinEntityHandle> CullList;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			FSeinCommandBrokerData* Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
			if (!Broker) return;

			// 1. Strip dead members (belt-and-suspenders — ProcessDeferredDestroys
			// already evicts on death, but members whose handle was released through
			// a non-destroy path would slip through without this).
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.RemoveAll([&](const FSeinEntityHandle& M)
			{
				return !World.GetEntityPool().IsValid(M);
			});
			if (Broker->Members.Num() != NumBefore)
			{
				Broker->bCapabilityMapDirty = true;
			}

			// 2. Update centroid from live members.
			if (Broker->Members.Num() > 0)
			{
				FFixedVector Sum;
				int32 Count = 0;
				for (const FSeinEntityHandle& M : Broker->Members)
				{
					if (const FSeinEntity* E = World.GetEntity(M))
					{
						Sum += E->Transform.GetLocation();
						++Count;
					}
				}
				if (Count > 0)
				{
					Broker->Centroid = Sum / FFixedPoint::FromInt(Count);
				}
			}

			// 3. Completion check: if executing, see if every member's primary
			// ability has ended. Passive abilities aren't tracked as "active" for
			// completion purposes — DESIGN §5 "Members only ever execute one active
			// ability at a time (dispatched from the current broker order)."
			if (Broker->bIsExecuting)
			{
				bool bAllDone = true;
				for (const FSeinEntityHandle& M : Broker->Members)
				{
					const FSeinAbilityData* AC = World.GetComponent<FSeinAbilityData>(M);
					if (AC && AC->ActiveAbility && AC->ActiveAbility->bIsActive)
					{
						bAllDone = false;
						break;
					}
				}
				if (bAllDone)
				{
					Broker->bIsExecuting = false;
					Broker->CurrentOrderTag = FGameplayTag();
					if (Broker->OrderQueue.Num() > 0)
					{
						Broker->OrderQueue.RemoveAt(0);
					}
				}
			}

			// 4. Dispatch next queued order if idle.
			if (!Broker->bIsExecuting && Broker->OrderQueue.Num() > 0 && Broker->Members.Num() > 0)
			{
				SeinCommandBrokerDispatch::DispatchFrontOrder(World, Handle, *Broker);
			}

			// 5. Cull if empty.
			if (Broker->Members.Num() == 0 && Broker->OrderQueue.Num() == 0 && !Broker->bIsExecuting)
			{
				CullList.Add(Handle);
			}
		});

		for (const FSeinEntityHandle& H : CullList)
		{
			World.DestroyEntity(H);
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 40; }
	virtual FName GetSystemName() const override { return TEXT("CommandBroker"); }
};
