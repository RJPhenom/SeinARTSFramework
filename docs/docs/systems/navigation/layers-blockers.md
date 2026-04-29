# Layers & Blockers

<span class="tag sim">SIM</span> Navigation supports per-agent layer masks — a designer-configurable bitfield that gates which dynamic blockers affect which units. The classic use case is "amphibious vehicles ignore water blockers"; the system generalizes to any agent-class vs. blocker-class filtering you need.

## The model

Every entity participating in nav carries a `NavLayerMask` (uint8, 8 bits). Every dynamic blocker carries a `BlockedNavLayerMask` (uint8, 8 bits). At `FindPath` time:

```
Blocker affects this agent  iff  (Agent.NavLayerMask & Blocker.BlockedNavLayerMask) != 0
```

Empty intersection → blocker is ignored for this agent's path. Set intersection → blocker stamps into the dynamic-blocked overlay and the path can't cross it.

Static (baked) cells are NOT layer-filtered — bake-time blocks affect everyone. Layers are a runtime concern.

## Layer slots

Bit 0 is the framework-reserved **Default** layer — always present, can't be removed. Bits 1..7 are designer-configurable in plugin settings:

**Project Settings → Plugins → SeinARTS → Navigation → Nav Layers:**

| Slot | Bit | Default name | Default state |
|---|---|---|---|
| 0 | 1 | "" (unused) | Disabled |
| 1 | 2 | "" (unused) | Disabled |
| 2 | 3 | "" (unused) | Disabled |
| 3 | 4 | "" (unused) | Disabled |
| 4 | 5 | "" (unused) | Disabled |
| 5 | 6 | "" (unused) | Disabled |
| 6 | 7 | "" (unused) | Disabled |

(That's 7 designer slots — `EditFixedSize`, framework-enforced, ship disabled.) Designers fill in:

- A name (e.g. "Amphibious", "HeavyVehicle", "InfantryOnly")
- An `bEnabled` flag
- A debug color (used by `Sein.Nav.Show.Layer` viz)

Renaming a slot is safe. **Reordering or inserting mid-array shifts every higher-slot bit and breaks replays + saves.** Only append or rename.

## The amphibious-vehicle pattern

Goal: a vehicle that can drive through water. Water blocker authored to gate Default-layer agents but not amphibious agents.

**Step 1: Add an "Amphibious" layer.** In plugin settings, set slot 0's `LayerName = "Amphibious"`, `bEnabled = true`, pick a debug color.

**Step 2: Author the water blocker.** Drop a placeholder actor over your water region — anything with `FSeinExtentsData`. Set:

- `bBlocksNav = true`
- `BlockedNavLayerMask` (bitmask picker): tick **only Default** (bit 0). Final value `0x01`.

**Step 3: Author the amphibious vehicle.** On its `FSeinMovementData`:

- `NavLayerMask` (bitmask picker): tick **only Amphibious** (bit 1, slot 0 → bit 1). Final value `0x02`.

**Result:** at FindPath time, `0x02 & 0x01 == 0`. The water blocker is dropped from the agent's overlay. The vehicle paths through.

For a unit that's **both** amphibious AND blocks like a tank (some heavy amphibious tracks): set its `NavLayerMask` to `0x03` (Default + Amphibious). It clears its own water blocker via the Amphibious bit, AND a different agent's path filter still sees it as a Default-layer blocker.

## How the per-tick stamping works

`FSeinNavBlockerStampSystem` runs every PreTick at priority 7 (after the spatial hash rebuild, before any FindPath). It walks every entity and:

1. Checks for `FSeinExtentsData` with `bBlocksNav` set
2. Expands each enabled `Shapes` entry into one `FSeinDynamicBlocker`
3. Each blocker carries: shape geometry, owning entity center + rotation, owner handle (for self-exclusion), `BlockedNavLayerMask` from the extents component
4. Pushes the flat blocker list to `USeinNavigation::SetDynamicBlockers`

**No `FSeinExtentsData`?** The system falls back to using `FSeinMovementData::FootprintRadius` to synthesize a radial blocker (Default layer only, mask `0x01`). This is the implicit "any moving unit blocks pathing" behavior — vehicles get a sensible default footprint stamp without designer authoring. Once the designer adds an `FSeinExtentsData` and sets `bBlocksNav = false`, the fallback stops applying.

## Self-exclusion

Without it, a tank pathing from inside its own footprint would never find a path out. `FindPath` builds the dynamic-blocked overlay at request time, skipping any blocker whose `Owner == Request.Requester`.

`USeinMoveToAction` populates `Request.Requester` automatically:

```cpp
FSeinPathRequest Req;
Req.Start = Entity->Transform.GetLocation();
Req.End = Destination;
Req.Requester = OwnerEntity;            // self-exclusion
Req.AgentNavLayerMask = MoveComp->NavLayerMask;  // layer filter
```

If you call `FindPath` directly, do the same.

## Footprint vs. extents

There are two related concepts that catch people:

- **`FSeinMovementData::FootprintRadius`** — used for steering (penetration resolution + avoidance perception). Single radial value. Does NOT drive nav blocking unless no `FSeinExtentsData` is authored.
- **`FSeinExtentsData::Shapes`** — the authoritative blocker geometry. Can be radial, rectangular, or compound. `bBlocksNav` flag opts in. `BlockedNavLayerMask` controls layer filtering.

Authoring both is normal and expected. Footprint drives "don't physically clip into other units"; extents drive "what the path planner sees as blocked." They're independent decisions.

## Debug viz

`Sein.Nav.Show 1` paints all dynamic blockers as orange cells in the editor + PIE viewports. Per-blocker color is derived from its `BlockedNavLayerMask` against plugin-settings layer colors — so a Default-only water blocker renders one color, an Amphibious-only blocker another, a multi-layer blocker a blend or stripe.

`Sein.Nav.Show.Layer <bit>` filters the display to a single layer:

```
Sein.Nav.Show.Layer 0    # Show only blockers that gate the Default layer
Sein.Nav.Show.Layer 1    # Show only blockers gating layer slot 0 (e.g. Amphibious)
Sein.Nav.Show.Layer       # No args → reset to "show all"
```

Useful for "what blocks my Amphibious unit?" audits.

## Extents component as the authoring surface

For Blueprint workflow, designers add a `USeinExtentsComponent` to their actor BP — drag-and-drop component, tick `Blocks Nav`, optionally tick `Blocks Fog Of War` (independent flag). The component's CDO inspector (a `FSeinExtentsDataDetails` customization) shows the layer-mask bitfield as a name-labeled checklist resolved from plugin settings, so designers see "Amphibious", "HeavyVehicle", etc. instead of bit numbers.

## Don't reorder

Saying it once more because it bites people: **NavLayers slots are positional in the bitfield**. Renaming a slot is fine — the underlying bit stays the same, only the display name changes. Reordering, inserting mid-array, or deleting slots all shift every higher-slot bit. That breaks every saved game, replay, and `FSeinExtentsData::BlockedNavLayerMask` already assigned in your project's BPs.

If you need to retire a layer: leave the slot, blank its name, set `bEnabled = false`. The bit stays consumed but the layer is effectively dead.
