/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimMutationBPFL.h
 * @brief   Restricted-access Blueprint Function Library for mutating sim-side
 *          component state. Callable only from `USeinAbility` and `USeinEffect`
 *          Blueprint graphs (enforced at BP-compile time by RestrictedToClasses).
 *          Each function additionally calls `SEIN_CHECK_SIM()` as a runtime
 *          backstop — functions invoked from outside a sim tick assert in
 *          dev builds.
 *
 *          Prefer field-level setters over whole-struct setters when a field-
 *          level version exists: whole-struct setters clobber every field and
 *          are footguns if another system is also mutating the same entity on
 *          the same tick.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Components/SeinCombatData.h"
#include "Components/SeinMovementData.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinProductionData.h"
#include "Components/SeinSquadData.h"
#include "Components/SeinSquadMemberData.h"
#include "Components/SeinResourceIncomeData.h"
#include "SeinSimMutationBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Sim Mutation Library", RestrictedToClasses = "SeinAbility,SeinEffect"))
class SEINARTSCOREENTITY_API USeinSimMutationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Whole-struct setters (escape hatches — clobber every field; prefer field-level where available)
	// ====================================================================================================

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Combat Data"))
	static bool SeinSetCombatData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinCombatData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Movement Data"))
	static bool SeinSetMovementData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinMovementData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Ability Data"))
	static bool SeinSetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinAbilityData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Production Data"))
	static bool SeinSetProductionData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinProductionData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Data"))
	static bool SeinSetSquadData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinSquadData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Member Data"))
	static bool SeinSetSquadMemberData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinSquadMemberData& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Resource Income Data"))
	static bool SeinSetResourceIncomeData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinResourceIncomeData& NewData);

	/** Generic whole-struct overwrite — escape hatch for USeinStructComponent-backed
	 *  designer components. Prefer typed setters for known component types. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Component"))
	static bool SeinSetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, const FInstancedStruct& NewData);

	// Field-level setters
	// ====================================================================================================
	//
	// NOTE: Combat + Ability field-level setters are intentionally omitted.
	//   - Combat: FSeinCombatData is pre-§10-reframe (pending Session 2.5 refactor to Health/MaxHealth).
	//             Damage/heal goes through SeinApplyDamage/SeinApplyHealing in SeinCombatBPFL.
	//   - Ability: mutating another ability's runtime state is a footgun. Abilities control their
	//              own lifecycle via OnTick/OnEnd and the activate/cancel command path.

	// ─── Movement field-level ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Move Speed"))
	static bool SeinSetMoveSpeed(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedPoint NewSpeed);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Movement Target"))
	static bool SeinSetMovementTarget(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector NewTarget);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Acceleration"))
	static bool SeinSetAcceleration(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedPoint NewAcceleration);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Turn Rate"))
	static bool SeinSetTurnRate(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedPoint NewTurnRate);

	// ─── Production field-level ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Rally Point"))
	static bool SeinSetRallyPoint(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector NewRallyPoint);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Current Build Progress"))
	static bool SeinSetCurrentBuildProgress(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedPoint NewProgress);

	// ─── Squad field-level ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Leader"))
	static bool SeinSetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewLeader);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Add Squad Member"))
	static bool SeinAddSquadMember(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewMember);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Squad Member"))
	static bool SeinRemoveSquadMember(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle MemberToRemove);

	// ─── Squad member field-level ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Formation Offset"))
	static bool SeinSetFormationOffset(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle, FFixedVector NewOffset);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
