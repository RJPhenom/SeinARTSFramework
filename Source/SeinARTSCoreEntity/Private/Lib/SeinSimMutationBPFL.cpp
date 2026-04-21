/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimMutationBPFL.cpp
 * @brief   Implementation of the restricted sim-mutation BPFL.
 */

#include "Lib/SeinSimMutationBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinSimContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinSimMutationBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

// Templated helper: whole-struct write. Bails on invalid handle / missing storage.
namespace
{
	template<typename T>
	bool WriteWholeStruct(const UObject* WorldContextObject, FSeinEntityHandle Handle, const T& NewData, const TCHAR* FnName)
	{
		SEIN_CHECK_SIM();
		UWorld* World = WorldContextObject ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		USeinWorldSubsystem* Subsystem = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("%s: no SeinWorldSubsystem"), FnName);
			return false;
		}
		T* Dst = Subsystem->GetComponent<T>(Handle);
		if (!Dst)
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("%s: entity %s invalid or has no %s"), FnName, *Handle.ToString(), *T::StaticStruct()->GetName());
			return false;
		}
		*Dst = NewData;
		return true;
	}
}

bool USeinSimMutationBPFL::SeinSetCombatData(const UObject* WCO, FSeinEntityHandle H, const FSeinCombatData& D)         { return WriteWholeStruct(WCO, H, D, TEXT("SetCombatData")); }
bool USeinSimMutationBPFL::SeinSetMovementData(const UObject* WCO, FSeinEntityHandle H, const FSeinMovementData& D)     { return WriteWholeStruct(WCO, H, D, TEXT("SetMovementData")); }
bool USeinSimMutationBPFL::SeinSetAbilityData(const UObject* WCO, FSeinEntityHandle H, const FSeinAbilityData& D)       { return WriteWholeStruct(WCO, H, D, TEXT("SetAbilityData")); }
bool USeinSimMutationBPFL::SeinSetProductionData(const UObject* WCO, FSeinEntityHandle H, const FSeinProductionData& D) { return WriteWholeStruct(WCO, H, D, TEXT("SetProductionData")); }
bool USeinSimMutationBPFL::SeinSetSquadData(const UObject* WCO, FSeinEntityHandle H, const FSeinSquadData& D)           { return WriteWholeStruct(WCO, H, D, TEXT("SetSquadData")); }
bool USeinSimMutationBPFL::SeinSetSquadMemberData(const UObject* WCO, FSeinEntityHandle H, const FSeinSquadMemberData& D) { return WriteWholeStruct(WCO, H, D, TEXT("SetSquadMemberData")); }
bool USeinSimMutationBPFL::SeinSetResourceIncomeData(const UObject* WCO, FSeinEntityHandle H, const FSeinResourceIncomeData& D) { return WriteWholeStruct(WCO, H, D, TEXT("SetResourceIncomeData")); }

bool USeinSimMutationBPFL::SeinSetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, const FInstancedStruct& NewData)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: no subsystem or null struct type"));
		return false;
	}
	if (NewData.GetScriptStruct() != StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: NewData type %s doesn't match requested %s"),
			NewData.GetScriptStruct() ? *NewData.GetScriptStruct()->GetName() : TEXT("<null>"),
			*StructType->GetName());
		return false;
	}
	ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(StructType);
	if (!Storage)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: no storage registered for %s"), *StructType->GetName());
		return false;
	}
	void* Dst = Storage->GetComponentRaw(EntityHandle);
	if (!Dst)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: entity %s has no %s"), *EntityHandle.ToString(), *StructType->GetName());
		return false;
	}
	StructType->CopyScriptStruct(Dst, NewData.GetMemory());
	return true;
}

// ─── Movement field-level ───

bool USeinSimMutationBPFL::SeinSetMoveSpeed(const UObject* WCO, FSeinEntityHandle H, FFixedPoint V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinMovementData* D = S->GetComponent<FSeinMovementData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetMoveSpeed: entity %s has no FSeinMovementData"), *H.ToString()); return false; }
	D->MoveSpeed = V;
	return true;
}

bool USeinSimMutationBPFL::SeinSetMovementTarget(const UObject* WCO, FSeinEntityHandle H, FFixedVector V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinMovementData* D = S->GetComponent<FSeinMovementData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetMovementTarget: entity %s has no FSeinMovementData"), *H.ToString()); return false; }
	D->TargetLocation = V;
	D->bHasTarget = true;
	return true;
}

bool USeinSimMutationBPFL::SeinSetAcceleration(const UObject* WCO, FSeinEntityHandle H, FFixedPoint V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinMovementData* D = S->GetComponent<FSeinMovementData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetAcceleration: entity %s has no FSeinMovementData"), *H.ToString()); return false; }
	D->Acceleration = V;
	return true;
}

bool USeinSimMutationBPFL::SeinSetTurnRate(const UObject* WCO, FSeinEntityHandle H, FFixedPoint V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinMovementData* D = S->GetComponent<FSeinMovementData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetTurnRate: entity %s has no FSeinMovementData"), *H.ToString()); return false; }
	D->TurnRate = V;
	return true;
}

// ─── Production field-level ───

bool USeinSimMutationBPFL::SeinSetRallyPoint(const UObject* WCO, FSeinEntityHandle H, FFixedVector V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinProductionData* D = S->GetComponent<FSeinProductionData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetRallyPoint: entity %s has no FSeinProductionData"), *H.ToString()); return false; }
	D->RallyTarget.bIsEntityTarget = false;
	D->RallyTarget.Location = V;
	D->RallyTarget.EntityTarget = FSeinEntityHandle();
	return true;
}

bool USeinSimMutationBPFL::SeinSetCurrentBuildProgress(const UObject* WCO, FSeinEntityHandle H, FFixedPoint V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinProductionData* D = S->GetComponent<FSeinProductionData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetCurrentBuildProgress: entity %s has no FSeinProductionData"), *H.ToString()); return false; }
	D->CurrentBuildProgress = V;
	return true;
}

// ─── Squad field-level ───

bool USeinSimMutationBPFL::SeinSetSquadLeader(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewLeader)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinSquadData* D = S->GetComponent<FSeinSquadData>(SquadHandle);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetSquadLeader: squad %s has no FSeinSquadData"), *SquadHandle.ToString()); return false; }
	D->Leader = NewLeader;
	return true;
}

bool USeinSimMutationBPFL::SeinAddSquadMember(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewMember)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinSquadData* D = S->GetComponent<FSeinSquadData>(SquadHandle);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("AddSquadMember: squad %s has no FSeinSquadData"), *SquadHandle.ToString()); return false; }
	D->Members.AddUnique(NewMember);
	return true;
}

bool USeinSimMutationBPFL::SeinRemoveSquadMember(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle MemberToRemove)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinSquadData* D = S->GetComponent<FSeinSquadData>(SquadHandle);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("RemoveSquadMember: squad %s has no FSeinSquadData"), *SquadHandle.ToString()); return false; }
	D->Members.Remove(MemberToRemove);
	return true;
}

// ─── Squad member field-level ───

bool USeinSimMutationBPFL::SeinSetFormationOffset(const UObject* WCO, FSeinEntityHandle H, FFixedVector V)
{
	SEIN_CHECK_SIM();
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	FSeinSquadMemberData* D = S->GetComponent<FSeinSquadMemberData>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetFormationOffset: member %s has no FSeinSquadMemberData"), *H.ToString()); return false; }
	D->FormationOffset = V;
	return true;
}
