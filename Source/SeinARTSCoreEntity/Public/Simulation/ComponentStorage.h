/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    ComponentStorage.h
 * @brief   Slot-indexed component storage keyed by FSeinEntityHandle.
 *
 * Flat arrays parallel to the entity pool: O(1) add/remove/get by slot index,
 * cache-friendly iteration over active slots via a TBitArray.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/SeinEntityHandle.h"
#include "UObject/UnrealType.h"

/**
 * Abstract interface for entity-handle-keyed component storage.
 */
class SEINARTSCOREENTITY_API ISeinComponentStorage
{
public:
	virtual ~ISeinComponentStorage() = default;

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
 * Component storage for reflection-based access, used by the runtime.
 *
 * Component types are discovered at entity spawn by walking the Blueprint
 * CDO's USeinActorComponents and injecting the struct each Resolve() returns.
 * Payloads are stored as raw bytes, with construction/copy/destroy dispatched
 * through UScriptStruct so TArrays, UPROPERTY references, etc. are handled
 * correctly.
 */
class SEINARTSCOREENTITY_API FSeinGenericComponentStorage : public ISeinComponentStorage
{
public:
	FSeinGenericComponentStorage(UScriptStruct* InStructType, int32 InitialCapacity = 0)
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

	virtual ~FSeinGenericComponentStorage() override
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
