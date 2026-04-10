/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisualEvent.cpp
 * @brief   Implementation of visual event factory methods and event queue.
 */

#include "Events/SeinVisualEvent.h"

// -----------------------------------------------------------------------------
// FSeinVisualEvent factory methods
// -----------------------------------------------------------------------------

FSeinVisualEvent FSeinVisualEvent::MakeSpawnEvent(FSeinEntityHandle Entity, FFixedVector InLocation)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntitySpawned;
	Event.PrimaryEntity = Entity;
	Event.Location = InLocation;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeDestroyEvent(FSeinEntityHandle Entity)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntityDestroyed;
	Event.PrimaryEntity = Entity;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeDamageEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Damage)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::DamageTaken;
	Event.PrimaryEntity = Target;
	Event.SecondaryEntity = Source;
	Event.Value = Damage;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeAbilityEvent(FSeinEntityHandle Entity, FGameplayTag AbilityTag, bool bActivated)
{
	FSeinVisualEvent Event;
	Event.Type = bActivated ? ESeinVisualEventType::AbilityActivated : ESeinVisualEventType::AbilityEnded;
	Event.PrimaryEntity = Entity;
	Event.Tag = AbilityTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeProductionEvent(FSeinEntityHandle Building, FGameplayTag ArchetypeTag, bool bCompleted)
{
	FSeinVisualEvent Event;
	Event.Type = bCompleted ? ESeinVisualEventType::ProductionCompleted : ESeinVisualEventType::ProductionStarted;
	Event.PrimaryEntity = Building;
	Event.Tag = ArchetypeTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeEffectEvent(FSeinEntityHandle Target, FGameplayTag EffectTag, bool bApplied)
{
	FSeinVisualEvent Event;
	Event.Type = bApplied ? ESeinVisualEventType::EffectApplied : ESeinVisualEventType::EffectRemoved;
	Event.PrimaryEntity = Target;
	Event.Tag = EffectTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeTechResearchedEvent(FSeinPlayerID Player, FGameplayTag TechTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::TechResearched;
	Event.PlayerID = Player;
	Event.Tag = TechTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakePingEvent(FSeinPlayerID Player, FFixedVector InLocation, FSeinEntityHandle OptionalTarget)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::Ping;
	Event.PlayerID = Player;
	Event.Location = InLocation;
	Event.PrimaryEntity = OptionalTarget;
	return Event;
}

// -----------------------------------------------------------------------------
// FSeinVisualEventQueue
// -----------------------------------------------------------------------------

void FSeinVisualEventQueue::Enqueue(const FSeinVisualEvent& Event)
{
	Events.Add(Event);
}

TArray<FSeinVisualEvent> FSeinVisualEventQueue::Flush()
{
	TArray<FSeinVisualEvent> Flushed = MoveTemp(Events);
	Events.Reset();
	return Flushed;
}

int32 FSeinVisualEventQueue::Num() const
{
	return Events.Num();
}
