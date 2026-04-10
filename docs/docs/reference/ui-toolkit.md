# UI Toolkit Nodes

<span class="tag render">RENDER</span> All functions in `USeinUIBPFL`. These are render-side utilities for building RTS interfaces.

## Entity Display Helpers

Functions that read archetype data for UI display. These resolve through the actor bridge to the entity's `USeinArchetypeDefinition`.

### SeinGetEntityDisplayName

```
SeinGetEntityDisplayName(Handle) → FText
```

Returns the display name from the entity's archetype definition. Returns empty text if the handle is invalid.

---

### SeinGetEntityIcon

```
SeinGetEntityIcon(Handle) → UTexture2D*
```

Returns the icon texture from the archetype. Used for selection panels, minimap markers, and action bars.

---

### SeinGetEntityPortrait

```
SeinGetEntityPortrait(Handle) → UTexture2D*
```

Returns the portrait texture. Typically a larger image than the icon, used in unit info panels.

---

### SeinGetEntityArchetypeTag

```
SeinGetEntityArchetypeTag(Handle) → FGameplayTag
```

Returns the archetype's gameplay tag (e.g., `Unit.Infantry.Rifleman`). Useful for grouping selected units by type.

---

### SeinGetEntityRelation

```
SeinGetEntityRelation(Handle, PlayerID) → ESeinRelation
```

Returns the relationship between an entity and a player: `Friendly`, `Enemy`, or `Neutral`. Compares entity owner against the given player ID.

## Conversion Helpers

### SeinFixedToFloat

```
SeinFixedToFloat(Value) → float
```

Converts `FFixedPoint` to `float`. Use for any sim value that needs to drive a UMG widget (progress bars, text display, etc.).

---

### SeinFixedVectorToVector

```
SeinFixedVectorToVector(Value) → FVector
```

Converts `FFixedVector` to Unreal `FVector`. Use when projecting sim positions to screen space or placing visual effects.

---

### SeinFormatResourceCost

```
SeinFormatResourceCost(Cost) → FText
```

Formats a `TMap<FName, float>` cost map into a display string. Example output: `"300 Manpower, 50 Fuel"`. Suitable for tooltip text.

## Screen Projection

### SeinWorldToScreen

```
SeinWorldToScreen(PlayerController, WorldPos, OutScreenPos) → bool
```

Projects a world position to screen coordinates. Returns `false` if the position is behind the camera. Wrapper around `ProjectWorldLocationToScreen` with null safety.

---

### SeinScreenToWorld

```
SeinScreenToWorld(PlayerController, ScreenPos, GroundZ, OutWorldPos) → bool
```

Deprojects a screen position to a world position on a horizontal plane at the given Z height. Useful for minimap click-to-world and right-click targeting.

---

### SeinProjectEntityToScreen

```
SeinProjectEntityToScreen(PlayerController, Handle, VerticalWorldOffset, OutScreenPos) → bool
```

Projects an entity's sim position to screen space, with an optional vertical offset (useful for placing UI above the unit's head). Converts from fixed-point internally.

---

### SeinIsEntityOnScreen

```
SeinIsEntityOnScreen(PlayerController, Handle, ScreenMargin) → bool
```

Returns `true` if the entity's projected screen position is within the viewport (with optional margin in pixels). Use to cull off-screen world-space widgets.

## Minimap Math

### SeinWorldToMinimap

```
SeinWorldToMinimap(WorldPos, WorldBoundsMin, WorldBoundsMax) → FVector2D
```

Converts a world XY position to normalized minimap UV coordinates (0–1 range). `WorldBoundsMin/Max` define the playable area.

---

### SeinMinimapToWorld

```
SeinMinimapToWorld(MinimapUV, WorldBoundsMin, WorldBoundsMax, GroundZ) → FVector
```

Converts minimap UV coordinates back to a world position. Use for minimap click-to-move commands.

---

### SeinGetCameraFrustumCorners

```
SeinGetCameraFrustumCorners(PlayerController, WorldBoundsMin, WorldBoundsMax, GroundZ) → TArray<FVector2D>
```

Returns the four corners of the camera's viewport frustum projected onto the ground plane, in minimap UV space. Use to draw the camera viewport rectangle on the minimap.

## Action Slot Builders

These functions produce `FSeinActionSlotData` structs — everything a UI button needs to display an ability or production item.

### FSeinActionSlotData

```cpp
USTRUCT(BlueprintType)
struct FSeinActionSlotData
{
    FText Name;                          // Display name
    FText Tooltip;                       // Tooltip text
    UTexture2D* Icon;                    // Button icon
    FGameplayTag ActionTag;              // Ability or archetype tag
    ESeinActionSlotState State;          // Available, OnCooldown, Disabled, etc.
    float CooldownPercent;               // 0–1 for cooldown sweep overlay
    TMap<FName, float> ResourceCost;     // Cost display
    int32 SlotIndex;                     // Position in the action bar
    FText HotkeyLabel;                   // Keyboard shortcut text
};
```

### ESeinActionSlotState

| Value | Meaning |
|-------|---------|
| `Empty` | No ability in this slot |
| `Available` | Can be activated |
| `OnCooldown` | Cooling down (show CooldownPercent) |
| `Disabled` | Cannot be used (prerequisites not met) |
| `Unaffordable` | Insufficient resources |
| `Active` | Currently executing |

---

### SeinBuildAbilitySlotData

```
SeinBuildAbilitySlotData(Entity, AbilityTag, SlotIndex) → FSeinActionSlotData
```

Builds action slot data for a single ability on an entity. Reads the ability's current state (cooldown, active, cost) and packages it for UI display.

---

### SeinBuildAllAbilitySlotData

```
SeinBuildAllAbilitySlotData(Entity) → TArray<FSeinActionSlotData>
```

Builds action slot data for all abilities on an entity. Returns an array ordered by ability index. Non-passive abilities only.

---

### SeinBuildProductionSlotData

```
SeinBuildProductionSlotData(Entity, PlayerID) → TArray<FSeinActionSlotData>
```

Builds action slot data for all producible items on a building entity. Checks prerequisites, affordability, and queue state per item. Use for building production panels.
