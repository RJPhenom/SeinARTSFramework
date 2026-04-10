/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinArchetypeDefinition.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of archetype definition component.
 */

#include "Data/SeinArchetypeDefinition.h"
#include "Engine/DataTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinArchetype, Log, All);

USeinArchetypeDefinition::USeinArchetypeDefinition()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = false;
}

TArray<FInstancedStruct> USeinArchetypeDefinition::GetResolvedComponents() const
{
	if (!bUseDataTableDefaults)
	{
		// Inline mode — return the array directly
		return Components;
	}

	// DataTable mode — resolve each reference into an FInstancedStruct
	TArray<FInstancedStruct> Resolved;
	Resolved.Reserve(DataTableComponents.Num());

	for (const FSeinComponentTableRef& Ref : DataTableComponents)
	{
		if (!Ref.IsValid())
		{
			continue;
		}

		const UScriptStruct* RowStruct = Ref.DataTable->GetRowStruct();
		if (!RowStruct)
		{
			UE_LOG(LogSeinArchetype, Warning, TEXT("DataTable %s has no row struct"), *Ref.DataTable->GetName());
			continue;
		}

		const uint8* RowData = Ref.DataTable->FindRowUnchecked(Ref.RowName);
		if (!RowData)
		{
			UE_LOG(LogSeinArchetype, Warning, TEXT("Row '%s' not found in DataTable %s"),
				*Ref.RowName.ToString(), *Ref.DataTable->GetName());
			continue;
		}

		// Create an FInstancedStruct from the row data
		FInstancedStruct Instance;
		Instance.InitializeAs(const_cast<UScriptStruct*>(RowStruct), RowData);
		Resolved.Add(MoveTemp(Instance));
	}

	return Resolved;
}

TArray<UScriptStruct*> USeinArchetypeDefinition::GetComponentTypes() const
{
	TArray<UScriptStruct*> Types;

	if (!bUseDataTableDefaults)
	{
		Types.Reserve(Components.Num());
		for (const FInstancedStruct& Comp : Components)
		{
			if (Comp.IsValid())
			{
				Types.Add(const_cast<UScriptStruct*>(Comp.GetScriptStruct()));
			}
		}
	}
	else
	{
		Types.Reserve(DataTableComponents.Num());
		for (const FSeinComponentTableRef& Ref : DataTableComponents)
		{
			if (Ref.IsValid() && Ref.DataTable->GetRowStruct())
			{
				Types.Add(const_cast<UScriptStruct*>(Ref.DataTable->GetRowStruct()));
			}
		}
	}

	return Types;
}

bool USeinArchetypeDefinition::HasComponents() const
{
	if (!bUseDataTableDefaults)
	{
		return Components.Num() > 0;
	}
	return DataTableComponents.Num() > 0;
}

bool USeinArchetypeDefinition::HasComponent(UScriptStruct* ComponentType) const
{
	if (!ComponentType)
	{
		return false;
	}

	if (!bUseDataTableDefaults)
	{
		for (const FInstancedStruct& Comp : Components)
		{
			if (Comp.IsValid() && Comp.GetScriptStruct() == ComponentType)
			{
				return true;
			}
		}
		return false;
	}

	for (const FSeinComponentTableRef& Ref : DataTableComponents)
	{
		if (Ref.IsValid() && Ref.DataTable->GetRowStruct() == ComponentType)
		{
			return true;
		}
	}
	return false;
}

FGameplayTag USeinArchetypeDefinition::ResolveCommandContext(const FGameplayTagContainer& Context) const
{
	// Find the highest-priority mapping whose RequiredContext tags are all present in Context.
	const FSeinCommandMapping* BestMatch = nullptr;

	for (const FSeinCommandMapping& Mapping : DefaultCommands)
	{
		if (!Mapping.AbilityTag.IsValid())
		{
			continue;
		}

		// All tags in RequiredContext must be present in the click context
		if (!Context.HasAll(Mapping.RequiredContext))
		{
			continue;
		}

		if (!BestMatch || Mapping.Priority > BestMatch->Priority)
		{
			BestMatch = &Mapping;
		}
	}

	return BestMatch ? BestMatch->AbilityTag : FallbackAbilityTag;
}
