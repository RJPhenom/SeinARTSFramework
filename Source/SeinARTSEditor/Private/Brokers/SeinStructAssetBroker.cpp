/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStructAssetBroker.cpp
 * @brief   Implementation of the Sein UDS → USeinStructComponent broker.
 */

#include "Brokers/SeinStructAssetBroker.h"
#include "Factories/SeinSimComponentFactory.h"
#include "Components/ActorComponents/SeinStructComponent.h"
#include "Components/ActorComponent.h"
#include "StructUtils/UserDefinedStruct.h"

UClass* FSeinStructAssetBroker::GetSupportedAssetClass()
{
	return UUserDefinedStruct::StaticClass();
}

bool FSeinStructAssetBroker::AssignAssetToComponent(UActorComponent* InComponent, UObject* InAsset)
{
	USeinStructComponent* StructComp = Cast<USeinStructComponent>(InComponent);
	UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(InAsset);
	if (!StructComp || !UDS)
	{
		return false;
	}

	// Refuse non-Sein UDSes — the broker fires for any UDS because UE matches
	// brokers by asset class only. Explicit reject here keeps the determinism
	// contract from leaking into random UDS assets in the project.
	if (!USeinSimComponentFactory::IsSeinDeterministicStruct(UDS))
	{
		return false;
	}

	StructComp->Data.InitializeAs(UDS);
	return true;
}

UObject* FSeinStructAssetBroker::GetAssetFromComponent(UActorComponent* InComponent)
{
	if (const USeinStructComponent* StructComp = Cast<USeinStructComponent>(InComponent))
	{
		return const_cast<UScriptStruct*>(StructComp->Data.GetScriptStruct());
	}
	return nullptr;
}
