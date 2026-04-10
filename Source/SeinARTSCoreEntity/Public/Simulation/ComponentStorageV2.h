/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    ComponentStorageV2.h
 * @brief   Slot-indexed component storage using entity handles.
 *
 * Replaces the map-based ComponentStorage with flat arrays parallel to the
 * entity pool. All operations are O(1) by slot index. A TBitArray tracks
 * which slots have an active component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/SeinEntityHandle.h"
#include "UObject/UnrealType.h"

/**
 * Abstract interface for V2 component storage.
 * Uses FSeinEntityHandle instead of FSeinID for all operations.
 */
class SEINARTSCOREENTITY_API ISeinComponentStorageV2
{
public:
	virtual ~ISeinComponentStorageV2() = default;

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) = 0;
	virtual void RemoveComponent(FSeinEntityHandle Handle) = 0;
	virtual bool HasComponent(FSeinEntityHandle Handle) const = 0;
	virtual void* GetComponentRaw(FSeinEntityHandle Handle) = 0;
	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const = 0;
	virtual uint32 ComputeHash() const = 0;
	virtual int32 GetComponentCount() const = 0;
	virtual void Clear() = 0;

	/** Alias for RemoveComponent — clearer intent when cleaning up a destroyed entity. */
	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) = 0;

	/** Grow internal arrays to accommodate a larger entity pool. */
	virtual void Grow(int32 NewCapacity) = 0;
};

/**
 * Slot-indexed component storage for a specific component type.
 *
 * Components are stored in a flat TArray indexed by the entity's slot index
 * (Handle.Index). A TBitArray tracks which slots have a valid component.
 * This gives O(1) add/remove/get and cache-friendly iteration.
 */
template<typename T>
class TSeinComponentStorageV2 : public ISeinComponentStorageV2
{
public:
	explicit TSeinComponentStorageV2(int32 InitialCapacity = 0)
		: ComponentCount(0)
	{
		if (InitialCapacity > 0)
		{
			const int32 TotalSlots = InitialCapacity + 1; // +1 for reserved slot 0
			Components.SetNum(TotalSlots);
			HasComponentBits.Init(false, TotalSlots);
		}
	}

	virtual ~TSeinComponentStorageV2() override = default;

	// --- ISeinComponentStorageV2 interface ---

	virtual void Grow(int32 NewCapacity) override
	{
		const int32 NewTotalSlots = NewCapacity + 1;
		const int32 OldSize = Components.Num();

		if (NewTotalSlots <= OldSize)
		{
			return;
		}

		Components.SetNum(NewTotalSlots);
		// TBitArray doesn't have SetNum — add the missing bits
		const int32 BitsToAdd = NewTotalSlots - HasComponentBits.Num();
		if (BitsToAdd > 0)
		{
			HasComponentBits.Add(false, BitsToAdd);
		}
	}

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		EnsureSlotCapacity(SlotIndex);

		if (!HasComponentBits[SlotIndex])
		{
			ComponentCount++;
		}

		if (ComponentData)
		{
			Components[SlotIndex] = *static_cast<const T*>(ComponentData);
		}
		else
		{
			Components[SlotIndex] = T{};
		}

		HasComponentBits[SlotIndex] = true;
	}

	virtual void RemoveComponent(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex])
		{
			HasComponentBits[SlotIndex] = false;
			Components[SlotIndex] = T{};
			ComponentCount--;
		}
	}

	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) override
	{
		RemoveComponent(Handle);
	}

	virtual bool HasComponent(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		return SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex];
	}

	virtual void* GetComponentRaw(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex])
		{
			return &Components[SlotIndex];
		}
		return nullptr;
	}

	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex])
		{
			return &Components[SlotIndex];
		}
		return nullptr;
	}

	virtual uint32 ComputeHash() const override
	{
		uint32 Hash = 0;
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint32>(SlotIndex)));
			Hash = HashCombine(Hash, GetTypeHash(Components[SlotIndex]));
		}
		return Hash;
	}

	virtual int32 GetComponentCount() const override
	{
		return ComponentCount;
	}

	virtual void Clear() override
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			Components[It.GetIndex()] = T{};
		}
		HasComponentBits.Init(false, HasComponentBits.Num());
		ComponentCount = 0;
	}

	// --- Typed accessors ---

	T* GetComponent(FSeinEntityHandle Handle)
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex])
		{
			return &Components[SlotIndex];
		}
		return nullptr;
	}

	const T* GetComponent(FSeinEntityHandle Handle) const
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < HasComponentBits.Num() && HasComponentBits[SlotIndex])
		{
			return &Components[SlotIndex];
		}
		return nullptr;
	}

	/**
	 * Add a component using typed data directly (avoids void* cast at call site).
	 */
	void AddTyped(FSeinEntityHandle Handle, const T& Component)
	{
		AddComponent(Handle, &Component);
	}

	/**
	 * Iterate all active components.
	 * @param Callback  Signature: void(uint32 SlotIndex, T& Component)
	 */
	template<typename Func>
	void ForEach(Func&& Callback)
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Callback(static_cast<uint32>(SlotIndex), Components[SlotIndex]);
		}
	}

	/** Const version of ForEach. */
	template<typename Func>
	void ForEach(Func&& Callback) const
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Callback(static_cast<uint32>(SlotIndex), Components[SlotIndex]);
		}
	}

private:
	/** Ensure the internal arrays can hold the given slot index. */
	void EnsureSlotCapacity(int32 SlotIndex)
	{
		const int32 RequiredSize = SlotIndex + 1;
		if (RequiredSize > Components.Num())
		{
			Components.SetNum(RequiredSize);
			const int32 BitsToAdd = RequiredSize - HasComponentBits.Num();
			if (BitsToAdd > 0)
			{
				HasComponentBits.Add(false, BitsToAdd);
			}
		}
	}

	TArray<T> Components;
	TBitArray<> HasComponentBits;
	int32 ComponentCount;
};

/**
 * Generic (non-templated) component storage for reflection-based access.
 * Used when component types are discovered dynamically at runtime (e.g., from
 * archetype definitions) rather than registered statically via RegisterComponentType<T>().
 *
 * Stores components as raw byte arrays, using UScriptStruct for type-safe
 * construction, copying, and destruction.
 */
class SEINARTSCOREENTITY_API FSeinGenericComponentStorageV2 : public ISeinComponentStorageV2
{
public:
	FSeinGenericComponentStorageV2(UScriptStruct* InStructType, int32 InitialCapacity = 0)
		: StructType(InStructType)
		, StructSize(InStructType ? InStructType->GetStructureSize() : 0)
		, ComponentCount(0)
	{
		check(StructType && StructSize > 0);
		if (InitialCapacity > 0)
		{
			const int32 TotalSlots = InitialCapacity + 1; // +1 for reserved slot 0
			Data.SetNumZeroed(TotalSlots * StructSize);
			HasComponentBits.Init(false, TotalSlots);
			SlotCapacity = TotalSlots;

			// Initialize all slots with default-constructed structs
			for (int32 i = 0; i < TotalSlots; ++i)
			{
				StructType->InitializeStruct(GetSlotPtr(i));
			}
		}
	}

	virtual ~FSeinGenericComponentStorageV2() override
	{
		// Destroy all initialized structs
		for (int32 i = 0; i < SlotCapacity; ++i)
		{
			StructType->DestroyStruct(GetSlotPtr(i));
		}
	}

	virtual void Grow(int32 NewCapacity) override
	{
		const int32 NewTotalSlots = NewCapacity + 1;
		if (NewTotalSlots <= SlotCapacity)
		{
			return;
		}

		const int32 OldCapacity = SlotCapacity;
		Data.SetNumZeroed(NewTotalSlots * StructSize);

		const int32 BitsToAdd = NewTotalSlots - HasComponentBits.Num();
		if (BitsToAdd > 0)
		{
			HasComponentBits.Add(false, BitsToAdd);
		}

		SlotCapacity = NewTotalSlots;

		// Initialize newly allocated slots
		for (int32 i = OldCapacity; i < NewTotalSlots; ++i)
		{
			StructType->InitializeStruct(GetSlotPtr(i));
		}
	}

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		EnsureSlotCapacity(SlotIndex);

		if (!HasComponentBits[SlotIndex])
		{
			ComponentCount++;
		}

		uint8* SlotPtr = GetSlotPtr(SlotIndex);
		if (ComponentData)
		{
			StructType->CopyScriptStruct(SlotPtr, ComponentData);
		}
		else
		{
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
		}

		HasComponentBits[SlotIndex] = true;
	}

	virtual void RemoveComponent(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			HasComponentBits[SlotIndex] = false;
			uint8* SlotPtr = GetSlotPtr(SlotIndex);
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
			ComponentCount--;
		}
	}

	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) override
	{
		RemoveComponent(Handle);
	}

	virtual bool HasComponent(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		return SlotIndex < SlotCapacity && HasComponentBits[SlotIndex];
	}

	virtual void* GetComponentRaw(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual uint32 ComputeHash() const override
	{
		uint32 Hash = 0;
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint32>(SlotIndex)));
			// Use raw memory hash — individual struct fields should implement GetTypeHash
			const uint8* SlotPtr = GetSlotPtr(SlotIndex);
			for (int32 i = 0; i < StructSize; ++i)
			{
				Hash = HashCombine(Hash, GetTypeHash(SlotPtr[i]));
			}
		}
		return Hash;
	}

	virtual int32 GetComponentCount() const override
	{
		return ComponentCount;
	}

	virtual void Clear() override
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			uint8* SlotPtr = GetSlotPtr(It.GetIndex());
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
		}
		HasComponentBits.Init(false, HasComponentBits.Num());
		ComponentCount = 0;
	}

	UScriptStruct* GetStructType() const { return StructType; }

private:
	void EnsureSlotCapacity(int32 SlotIndex)
	{
		const int32 RequiredSize = SlotIndex + 1;
		if (RequiredSize > SlotCapacity)
		{
			Grow(RequiredSize - 1);
		}
	}

	uint8* GetSlotPtr(int32 SlotIndex)
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	const uint8* GetSlotPtr(int32 SlotIndex) const
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	UScriptStruct* StructType;
	int32 StructSize;
	TArray<uint8> Data;
	TBitArray<> HasComponentBits;
	int32 SlotCapacity = 0;
	int32 ComponentCount;
};
