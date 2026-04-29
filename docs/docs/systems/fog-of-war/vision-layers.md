# Vision Layers

<span class="tag sim">SIM</span> Fog-of-war is multi-layered. Beyond standard "Normal" visibility, designers can configure up to 6 additional perception channels (Stealth, Thermal, Radar, Acoustic, Infrared, Detector — pick what your game needs). Each entity declares what layers it perceives and what layers it emits on.

## The layer model

The EVNNNNNN cell bitfield carries:

- Bit 0: **E** — Explored (sticky, set on first visibility)
- Bit 1: **V** — Visible-Normal (the everyday "in the line of sight" bit)
- Bits 2..7: **N0..N5** — designer-named custom layers

A cell having bit `b` set in player X's vision group means "player X has at least one source perceiving on layer `b` whose stamp covers this cell." Visibility is per-player.

## Source perception vs. emission

Every entity that participates in vision has two relevant masks:

- **Perception mask** — what layers this source's stamps emit on (V always, plus any custom layers)
- **Emission mask** — what layers this entity needs covered for it to be visible to a watching observer

A target entity `E` is visible to observer `O` iff:

```
∃ source S owned by O such that
  (S.PerceptionMask ∩ E.EmissionMask) ≠ ∅
  AND
  E's cell has any of those bits set in O's VisionGroup bitfield
```

Plain "Normal" units perceive on V, emit on V. Both happen — they show up.

## Plugin settings

**Project Settings → Plugins → SeinARTS → Fog Of War → Vision Layers** has 6 designer slots (`EditFixedSize`, framework-enforced):

| Slot | Bit | Default name | Default state |
|---|---|---|---|
| 0 | 2 (N0) | "" | Disabled |
| 1 | 3 (N1) | "" | Disabled |
| 2 | 4 (N2) | "" | Disabled |
| 3 | 5 (N3) | "" | Disabled |
| 4 | 6 (N4) | "" | Disabled |
| 5 | 7 (N5) | "" | Disabled |

Per slot:

- `LayerName` — designer name (e.g. "Stealth", "Thermal")
- `bEnabled` — only enabled slots respond to perception/emission queries
- `DebugColor` — viz color for `Sein.FogOfWar.Show.Layer <bit>`

The framework "Normal" layer is bit 1 (V) and is always present — it's not in this array. **Don't try to add Normal back** — it's a no-op.

Renaming a slot is safe (the bit stays the same, only the display name changes). **Reordering, inserting mid-array, or deleting shifts every higher-slot bit.** That breaks every saved game and replay where an entity already had a `EmissionLayerMask` value with bits in those slots. Only append or rename.

## Authoring a stealth pattern

Goal: a unit that's invisible to Normal-perceiving observers but visible to Stealth-detector observers. Classic "cloaked unit + reveal tower" RTS pattern.

**Step 1: Add a "Stealth" layer.** Plugin Settings → Vision Layers → slot 0 → `LayerName = "Stealth"`, `bEnabled = true`.

**Step 2: Author the cloaked unit.** On its `FSeinVisionData`:

- `EmissionLayerMask`: tick **only Stealth** (bit 2, slot 0 → bit 2). Final value `0x04`.
  - This unit is *not* emitting on V (bit 1), so Normal-perceiving observers never see it.
- Stamp configs: standard (the cloaked unit can still see normally).

**Step 3: Author the detector tower.** On its `FSeinVisionData`:

- Stamps include a Stealth-perception stamp (bit 2 in the stamp's `EmissionLayerMask`):

  ```
  Stamp 0 (Default vision):
    Shape: radial, radius 1500
    EmissionLayerMask: V (Normal)
  Stamp 1 (Stealth detection):
    Shape: radial, radius 800
    EmissionLayerMask: N0 (Stealth)
  ```

**Step 4: Result.** Cells within 800 units of the detector tower have V + N0 set in the owning player's bitfield. The cloaked unit's `EmissionLayerMask` is `Stealth`-only — visible iff the cell has the Stealth bit set. So:

- Outside the tower's 800-unit Stealth radius: cloaked unit invisible (no Stealth bit at its cell).
- Inside the tower's 800-unit radius: cloaked unit visible (Stealth bit set, mask intersects).
- Other Normal-perceiving units never see the cloak (their stamps only emit V; cloak emits Stealth-only; intersection empty).

## The full stealth-set decision

A cloaked unit usually wants to be:

- **Visible to its owner** — owner's Normal stamps still apply, but the unit emits Stealth-only. Solution: ownership filter on the visibility subsystem (already provided — owned actors are always visible regardless of fog state).
- **Visible to allies?** Per-faction call. If allies share vision, the same source-of-vision sees the cloaked emission with their own Stealth perception.
- **Visible to detectors?** Yes — that's the pattern above.
- **Invisible to enemy Normal** — yes, this is the default.

For "make it briefly visible when it attacks" UX: an ability OnActivate adds V to the unit's `EmissionLayerMask`, OnEnd removes it. See [Sources & Blockers](sources-blockers.md) for the runtime-mutation BPFL.

## Layer-aware blockers

Static and dynamic blockers carry their own `BlockedLayerMask` — which layer bits they occlude. Default impl bakes static blockers as `0xFE` (every vis bit). Designer-authored dynamic blockers (smoke grenades, etc.) set their own mask:

- Smoke that blocks Normal but lets Thermal through: `BlockedLayerMask = SEIN_FOW_BIT_NORMAL` (just V).
- Dense fog that blocks everything: `BlockedLayerMask = 0xFE` (all vis bits).
- Foliage that blocks Normal + Thermal but not Sound (acoustic detection): `BlockedLayerMask = (V | N0)` for whatever bit Acoustic isn't.

The shadowcast LoS pass tests blockers against the stamp's `StampBitMask` — a stamp emitting on N1 (Thermal) ignores blockers whose `BlockedLayerMask` doesn't include N1. So smoke that doesn't block Thermal lets Thermal stamps through.

## Querying with layer names

The BPFL accepts layer names by string, resolving against plugin settings:

```
SeinIsCellVisible(WorldContext, Observer, WorldPos, "Normal")     → V bit
SeinIsCellVisible(WorldContext, Observer, WorldPos, "Explored")   → E bit
SeinIsCellVisible(WorldContext, Observer, WorldPos, "Stealth")    → N0 (or whichever slot named "Stealth")
SeinIsCellVisible(WorldContext, Observer, WorldPos, "Thermal")    → whatever slot named "Thermal"
```

Unknown names return false. `LayerName` defaults to "Normal".

## Runtime mutation of emission

A unit's emission mask isn't a fixed thing — it can change at runtime to reflect cloak/decloak, ability states, etc. The fog BPFL exposes:

- `SeinGetEntityEmissionMask` — read current mask
- `SeinSetEntityEmissionMask` — overwrite
- `SeinAddEntityEmissionLayers` — OR in additional layers
- `SeinRemoveEntityEmissionLayers` — clear specific layers

Typical usage:

```
Cloak ability OnActivate:
  SetEntityEmissionMask(Self, N0)            # Stealth only

Cloak ability OnEnd:
  SetEntityEmissionMask(Self, V)             # Back to Normal

Stealth field aura OnApply (effect, additive):
  AddEntityEmissionLayers(Target, N0)        # Adds Stealth alongside whatever else

Stealth field aura OnRemove:
  RemoveEntityEmissionLayers(Target, N0)
```

Mask of `0` = entity is permanently invisible to everyone (no observer has any matching bit set, intersection always empty). Useful for scripted hidden units.

## Don't reorder

Same warning as nav layers: the slot's bit position is positional. Renaming = safe. Reordering / inserting / deleting = breaks every save with that mask serialized. If you need to retire a layer: leave the slot, blank the name, set `bEnabled = false`. The bit position stays consumed.
