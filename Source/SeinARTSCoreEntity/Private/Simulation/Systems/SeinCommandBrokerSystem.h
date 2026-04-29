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
		USeinAbility* Ability = AC->FindAbilityByTag(World, MD.AbilityTag);
		if (!Ability) return;

		if (Ability->IsOnCooldown()) return;
		if (!Ability->CanActivate()) return;

		// Broker-dispatched activation skips cost/cooldown-refund gating — the
		// broker-level order represents the "one player click" that already went
		// through ProcessCommands validation; per-member cost duplication is not
		// well-defined at V1. Designers can add per-member cost via their ability
		// OnActivate logic if they need it.
		// Cancel OTHER abilities with matching tags; explicitly skip self so a
		// duplicate broker dispatch doesn't cancel-then-reactivate on every
		// command frame (see the matching block in SeinWorldSubsystem::ProcessCommands).
		for (int32 OtherID : AC->AbilityInstanceIDs)
		{
			USeinAbility* Other = World.GetAbilityInstance(OtherID);
			if (Other && Other != Ability && Other->bIsActive &&
				Other->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
			{
				Other->CancelAbility();
			}
		}
		// Self-cancelling reissue: re-firing an already-active ability that
		// names its own tag cancels the prior run before the new one starts.
		if (Ability->bIsActive &&
			Ability->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
		{
			Ability->CancelAbility();
		}
		Ability->ActivateAbility(MD.TargetEntity, MD.TargetLocation);
		if (!Ability->bIsPassive)
		{
			// Find the ID of `Ability` in this entity's ability instances.
			int32 ActiveID = INDEX_NONE;
			for (int32 ID : AC->AbilityInstanceIDs)
			{
				if (World.GetAbilityInstance(ID) == Ability) { ActiveID = ID; break; }
			}
			AC->ActiveAbilityID = ActiveID;
		}
	}

	static void RebuildCapabilityMap(USeinWorldSubsystem& World, FSeinCommandBrokerData& Broker)
	{
		Broker.CapabilityMap.Reset();
		for (const FSeinEntityHandle& M : Broker.Members)
		{
			const FSeinAbilityData* AC = World.GetComponent<FSeinAbilityData>(M);
			if (!AC) continue;
			for (int32 ID : AC->AbilityInstanceIDs)
			{
				const USeinAbility* Ab = World.GetAbilityInstance(ID);
				if (Ab && Ab->AbilityTag.IsValid())
				{
					Broker.CapabilityMap.FindOrAdd(Ab->AbilityTag).Members.AddUnique(M);
				}
			}
		}
		Broker.bCapabilityMapDirty = false;
	}

	/** Build the effective member set for a queued order — TargetMembers if
	 *  non-empty (subset-targeted), else the broker's full Members. Subset
	 *  entries are also filtered for liveness (dead handles skipped). */
	static TArray<FSeinEntityHandle> BuildEffectiveMembers(const USeinWorldSubsystem& World,
		const FSeinCommandBrokerData& Broker,
		const FSeinBrokerQueuedOrder& Order)
	{
		if (Order.TargetMembers.Num() == 0)
		{
			return Broker.Members;
		}
		TArray<FSeinEntityHandle> Out;
		Out.Reserve(Order.TargetMembers.Num());
		for (const FSeinEntityHandle& H : Order.TargetMembers)
		{
			if (World.GetEntityPool().IsValid(H) && Broker.Members.Contains(H))
			{
				Out.Add(H);
			}
		}
		return Out;
	}

	/** Dispatch the front queued order via the broker's resolver. Returns true if
	 *  the order was dispatched (broker.bIsExecuting was flipped on). */
	static bool DispatchFrontOrder(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		FSeinCommandBrokerData& Broker)
	{
		if (Broker.OrderQueue.Num() == 0 || Broker.Members.Num() == 0) return false;
		USeinCommandBrokerResolver* Resolver = World.GetCommandBrokerResolver(Broker.ResolverID);
		if (!Resolver) return false;

		if (Broker.bCapabilityMapDirty)
		{
			RebuildCapabilityMap(World, Broker);
		}

		const FSeinBrokerQueuedOrder& Front = Broker.OrderQueue[0];

		// Build the effective member set. If subset-targeted and the targets are
		// all dead / no longer in the broker, pop the order and bail — nothing to
		// dispatch. The system tick will re-enter and dispatch the next order.
		const TArray<FSeinEntityHandle> Effective = BuildEffectiveMembers(World, Broker, Front);
		if (Effective.Num() == 0)
		{
			Broker.OrderQueue.RemoveAt(0);
			return false;
		}

		FSeinBrokerOrderInput Input;
		Input.Context = Front.Context;
		Input.TargetEntity = Front.TargetEntity;
		Input.TargetLocation = Front.TargetLocation;
		Input.FormationEnd = Front.FormationEnd;
		Input.EffectiveMembers = Effective;

		const FSeinBrokerDispatchPlan Plan = Resolver->ResolveDispatch(&World, BrokerHandle, Input);

		Broker.CurrentOrderContext = Front.Context;
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
			// a non-destroy path would slip through without this). Also strip dead
			// handles from each queued order's TargetMembers — subset-targeted
			// orders whose every target died fall through to the empty-subset
			// guard in DispatchFrontOrder and get popped silently.
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.RemoveAll([&](const FSeinEntityHandle& M)
			{
				return !World.GetEntityPool().IsValid(M);
			});
			if (Broker->Members.Num() != NumBefore)
			{
				Broker->bCapabilityMapDirty = true;
			}
			for (FSeinBrokerQueuedOrder& Order : Broker->OrderQueue)
			{
				if (Order.TargetMembers.Num() == 0) continue;
				Order.TargetMembers.RemoveAll([&](const FSeinEntityHandle& M)
				{
					return !World.GetEntityPool().IsValid(M);
				});
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

			// 3. Completion check: if executing, see if every EFFECTIVE member's
			// primary ability has ended. Subset-targeted orders only wait on
			// their target members — non-target members can stay idle (or be
			// running something else from a prior order) without blocking the
			// current order's completion. Passive abilities aren't tracked as
			// "active" for completion purposes — DESIGN §5 "Members only ever
			// execute one active ability at a time (dispatched from the current
			// broker order)."
			if (Broker->bIsExecuting && Broker->OrderQueue.Num() > 0)
			{
				const FSeinBrokerQueuedOrder& Front = Broker->OrderQueue[0];
				const TArray<FSeinEntityHandle> Effective =
					SeinCommandBrokerDispatch::BuildEffectiveMembers(World, *Broker, Front);

				bool bAllDone = true;
				for (const FSeinEntityHandle& M : Effective)
				{
					const FSeinAbilityData* AC = World.GetComponent<FSeinAbilityData>(M);
					const USeinAbility* Active = AC ? AC->GetActiveAbility(World) : nullptr;
					if (Active && Active->bIsActive)
					{
						bAllDone = false;
						break;
					}
				}
				if (bAllDone)
				{
					Broker->bIsExecuting = false;
					Broker->CurrentOrderContext = FGameplayTagContainer();
					Broker->OrderQueue.RemoveAt(0);
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
