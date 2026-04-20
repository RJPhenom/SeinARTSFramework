/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStructAssetBroker.h
 * @brief   Asset broker that wires a designer-authored Sein UDS into a
 *          USeinStructComponent on drag-drop to a SeinActor BP. Accepts
 *          only UDSes marked with the SeinDeterministic meta key.
 */

#pragma once

#include "CoreMinimal.h"
#include "ComponentAssetBroker.h"

class UActorComponent;
class UObject;
class UUserDefinedStruct;

class FSeinStructAssetBroker : public IComponentAssetBroker
{
public:
	virtual UClass* GetSupportedAssetClass() override;
	virtual bool AssignAssetToComponent(UActorComponent* InComponent, UObject* InAsset) override;
	virtual UObject* GetAssetFromComponent(UActorComponent* InComponent) override;
};
