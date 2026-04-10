/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		ComponentStorage.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Dynamic component storage system for entity components.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/EntityID.h"

/**
 * Abstract interface for component storage.
 * Allows subsystem to manage different component types polymorphically.
 */
class SEINARTSCOREENTITY_API ISeinComponentStorage
{
public:
	virtual ~ISeinComponentStorage() = default;

	/**
	 * Add a component for the given entity.
	 * @param EntityID - Entity to add component to
	 * @param ComponentData - Pointer to component data to copy
	 */
	virtual void AddComponent(FSeinID EntityID, const void* ComponentData) = 0;

	/**
	 * Remove component from entity.
	 * @param EntityID - Entity to remove component from
	 */
	virtual void RemoveComponent(FSeinID EntityID) = 0;

	/**
	 * Check if entity has this component.
	 * @param EntityID - Entity to check
	 * @return True if entity has this component
	 */
	virtual bool HasComponent(FSeinID EntityID) const = 0;

	/**
	 * Get raw pointer to component data for entity.
	 * @param EntityID - Entity to get component for
	 * @return Pointer to component data, or nullptr if not found
	 */
	virtual void* GetComponentRaw(FSeinID EntityID) = 0;
	virtual const void* GetComponentRaw(FSeinID EntityID) const = 0;

	/**
	 * Compute hash of all components for determinism validation.
	 * @return Combined hash of all component data
	 */
	virtual uint32 ComputeHash() const = 0;

	/**
	 * Get number of components stored.
	 * @return Component count
	 */
	virtual int32 GetComponentCount() const = 0;

	/**
	 * Clear all components.
	 */
	virtual void Clear() = 0;
};

/**
 * Template implementation of component storage for a specific component type.
 * Wraps TMap<FSeinID, T> and provides type-safe access.
 */
template<typename T>
class TSeinComponentStorage : public ISeinComponentStorage
{
private:
	TMap<FSeinID, T> Components;

public:
	virtual ~TSeinComponentStorage() override = default;

	// ISeinComponentStorage interface
	virtual void AddComponent(FSeinID EntityID, const void* ComponentData) override
	{
		if (ComponentData)
		{
			Components.Add(EntityID, *static_cast<const T*>(ComponentData));
		}
		else
		{
			Components.Add(EntityID, T{});
		}
	}

	virtual void RemoveComponent(FSeinID EntityID) override
	{
		Components.Remove(EntityID);
	}

	virtual bool HasComponent(FSeinID EntityID) const override
	{
		return Components.Contains(EntityID);
	}

	virtual void* GetComponentRaw(FSeinID EntityID) override
	{
		return Components.Find(EntityID);
	}

	virtual const void* GetComponentRaw(FSeinID EntityID) const override
	{
		return Components.Find(EntityID);
	}

	virtual uint32 ComputeHash() const override
	{
		uint32 Hash = 0;
		for (const auto& Pair : Components)
		{
			Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
			Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
		}
		return Hash;
	}

	virtual int32 GetComponentCount() const override
	{
		return Components.Num();
	}

	virtual void Clear() override
	{
		Components.Empty();
	}

	// Type-safe accessors
	/**
	 * Get component for entity (mutable).
	 * @param EntityID - Entity to get component for
	 * @return Pointer to component, or nullptr if not found
	 */
	T* GetComponent(FSeinID EntityID)
	{
		return Components.Find(EntityID);
	}

	/**
	 * Get component for entity (const).
	 * @param EntityID - Entity to get component for
	 * @return Pointer to component, or nullptr if not found
	 */
	const T* GetComponent(FSeinID EntityID) const
	{
		return Components.Find(EntityID);
	}

	/**
	 * Get all components (for system iteration).
	 * @return Reference to internal component map
	 */
	TMap<FSeinID, T>& GetAllComponents()
	{
		return Components;
	}

	const TMap<FSeinID, T>& GetAllComponents() const
	{
		return Components;
	}
};
