# Custom Fog of War Implementations

<span class="tag advanced">ADVANCED</span> The shipped 2D-grid + shadowcast impl handles RTS-typical fog-of-war well. For projects that need volumetric fog, GPU-driven raymarched visibility, simpler "everyone sees everything" mode, or radically different perception semantics, `USeinFogOfWar` is replaceable end-to-end.

## What you implement

Subclass `USeinFogOfWar`. Override the methods you need; the rest stay as the base's no-op / permissive defaults.

### Required (no useful default)

| Method | Purpose |
|---|---|
| `BeginBake(UWorld* World) → bool` | Kick off the bake. Sync or async. Return true if started. |
| `IsBaking() const → bool` | Bake-in-progress probe |
| `GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const → uint8` | Visibility query — returns the EVNNNNNN byte at the cell containing `WorldPos` from `Observer`'s vision group |
| `GetAssetClass() const → TSubclassOf<USeinFogOfWarAsset>` | Your concrete asset subclass |
| `LoadFromAsset(USeinFogOfWarAsset*) → void` | Hoist baked data into your runtime arrays |
| `HasRuntimeData() const → bool` | True iff runtime state is populated |
| `TickStamps(UWorld* World) → void` | Recompute per-cell bits from current sources + blockers. Called by the subsystem at `VisionTickInterval` cadence. |

### Strongly recommended

| Method | Why |
|---|---|
| `IsEntityVisibleTo(FSeinPlayerID Observer, FSeinEntityHandle Target) const → bool` | Specialized entity-visibility test. Default returns true (permissive). Override to consider entity's `EmissionLayerMask`. |
| `GetEntityVisibleBits(FSeinPlayerID Observer, USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const → uint8` | Volumetric visibility — ORs cell bits across the target's `FSeinExtentsData::Stamps`. Default falls back to single-point at entity transform. |
| `InitGridFromVolumes(UWorld* World) → void` | Auto-size your grid pre-bake so debug viz works. No-op default. |

### Optional

| Method | Default behavior |
|---|---|
| `RegisterSource` / `UpdateSource` / `UnregisterSource` | Explicit source lifecycle. Default no-op. The shipped impl walks the pool itself in `TickStamps` rather than receiving explicit notifications. |
| `RegisterBlocker` / `UpdateBlocker` / `UnregisterBlocker` | Explicit blocker lifecycle. Same pattern. |
| `RequestCancelBake() → void` | No-op. Override to cancel async bakes. |
| `OnFogOfWarInitialized(UWorld*)` / `OnFogOfWarDeinitialized()` | Lifecycle hooks |
| `CollectDebugCellQuads(Observer, BitIndex, ...)` | Debug viz collector. No-op default = no fog viz under `Sein.FogOfWar.Show`. |
| `CustomizeVolumeDetails(IDetailLayoutBuilder&, ASeinFogOfWarVolume*)` | Editor extensibility. Add custom rows below the framework's bake button. |

## What you DON'T have to implement

- **Volume actor**: keep using `ASeinFogOfWarVolume`. Polymorphic asset reference.
- **Subsystem**: `USeinFogOfWarSubsystem` instantiates whatever class plugin settings point at.
- **Visibility subsystem**: `USeinFogOfWarVisibilitySubsystem` (the render-side actor toggler) reads through `IsEntityVisibleTo` — works with any impl.
- **Reader BPFL**: `USeinFogOfWarBPFL` calls `GetCellBitfield` + `IsEntityVisibleTo`. Works with any impl.
- **Bake button**: `FSeinFogOfWarVolumeDetails` calls into your `BeginBake` via the abstract base.
- **Cross-module delegate**: `USeinFogOfWarSubsystem::BindSimDelegates` wires `USeinWorldSubsystem::LineOfSightResolver` to your `IsCellVisible`. Sim ability validation works against any impl.

## Build dependencies for your custom module

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "GameplayTags",
    "SeinARTSCore",          // FFixedPoint, FFixedVector
    "SeinARTSCoreEntity",    // FSeinEntityHandle, USeinWorldSubsystem,
                             // FSeinVisionData, FSeinExtentsData (if you read sim components)
    "SeinARTSFogOfWar"       // The abstract base + types + ASeinFogOfWarVolume
});

if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.AddRange(new string[] {
        "Slate", "SlateCore",
        "UnrealEd", "AssetRegistry",
        "PropertyEditor"  // Only if you override CustomizeVolumeDetails
    });
}
```

## Picking your class in settings

After your subclass is registered:

**Project Settings → Plugins → SeinARTS → Fog Of War → Fog Of War Class**

Class picker filters to `USeinFogOfWar` subclasses via `MetaClass = "/Script/SeinARTSFogOfWar.SeinFogOfWar"`. Pick yours.

The subsystem reads this on Initialize. If empty or stale, falls back to `USeinFogOfWarDefault`.

## A minimal skeleton

```cpp
// MyFog.h
#include "SeinFogOfWar.h"
#include "MyFog.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "My Custom Fog"))
class MYMODULE_API UMyFog : public USeinFogOfWar
{
    GENERATED_BODY()
public:
    virtual TSubclassOf<USeinFogOfWarAsset> GetAssetClass() const override;
    virtual bool BeginBake(UWorld* World) override;
    virtual bool IsBaking() const override { return bBaking; }
    virtual void LoadFromAsset(USeinFogOfWarAsset* Asset) override;
    virtual bool HasRuntimeData() const override { return /* state populated */; }

    virtual void TickStamps(UWorld* World) override;
    virtual uint8 GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const override;

private:
    bool bBaking = false;
    // ... your impl-specific runtime state ...
};
```

## Determinism notes

The base contract — same as nav:

- All sim-side queries are `FFixedPoint`/`FFixedVector` end-to-end.
- Stamp updates run from `OnSimTickCompleted` so all clients stamp against identical source snapshots.
- No float comparisons inside the stamp pass or the LoS test.
- Bake is editor-only; runtime never re-traces float collision.

A custom impl that wants GPU-driven raymarching for the *render-side* fog viz is fine — that's a render-only concern. But the **sim-visible state** (the bitfield observed by `GetCellBitfield`) must be `FFixedPoint` deterministic. Otherwise different clients see different units in different places, and an ability validation that asks "is this enemy visible?" gets different answers on different machines = desync.

The split is: deterministic sim state for gameplay correctness, free-form (float, async, GPU) anything for visualization.

## Things to verify

- [ ] `Sein.FogOfWar.Show 1` toggles your debug viz (or shows nothing if you don't override the collectors)
- [ ] Designer drops `ASeinFogOfWarVolume`, clicks "Bake Fog Of War", your `BeginBake` is called
- [ ] After bake, runtime asset survives a level reload
- [ ] In PIE with multiple players, each player sees their own units regardless of fog (the visibility subsystem auto-handles this)
- [ ] Enemy units enter/exit visibility correctly as your stamps update
- [ ] Ability validation rejects unseen targets (check `LineOfSightResolver` is wired — it should be, automatically)
- [ ] `GetCellBitfield` is fully `FFixedPoint` end-to-end (desync risk otherwise)

## Common custom impl patterns

### Simpler grid (no shadowcast)

For a "no LoS, just radius" fog (every cell within X units of any source = visible), strip out the LoS pass:

```cpp
void UMyFog::TickStamps(UWorld* World) override
{
    // For each source: stamp every cell within radius, no LoS check.
    // Cells get bits set in their player's group; refcount for transitions.
}
```

Cheaper, less interesting. Useful for prototypes or genres where height-aware fog isn't a fit.

### Volumetric / 3D

Replace the 2D grid with a 3D voxel grid or sparse octree. Cell coords become `(X, Y, Z)`. `GetCellBitfield` takes a `FFixedVector` instead of an XY — already does, just use the Z.

The bake gets meatier (per-voxel terrain/blocker capture) and the LoS pass becomes 3D Bresenham. Costs scale by the Z dimension. Worth it only if your camera + level design genuinely need 3D fog.

### GPU-driven render fog

The simplest path: keep the deterministic 2D grid for *gameplay queries*, but read the grid into a GPU texture each render frame and use that as input to a render-side raymarch shader for the visual fog.

Sim is unchanged. The visibility subsystem still toggles actor visibility from the deterministic state. The texture upload + shader is pure render-side work.

This gives you "AAA-looking volumetric fog" without a determinism rewrite. Recommended path if your project wants better-looking fog without giving up the lockstep model.

### Per-team fog (vs. per-player)

Every observer in the API is `FSeinPlayerID`. If you want per-team fog (allies share vision automatically), do it at the read-side — when querying for a player, OR together every cell bitfield from their team's `FSeinPlayerID`s before returning. Cheap; doesn't require restructuring the storage.

`USeinFogOfWarVisibilitySubsystem` would also need to consider team membership when deciding whether to hide an actor.

## When NOT to replace it

If `USeinFogOfWarDefault` works for your project, replacing it is a multi-week investment with a long determinism tail. Try to push the default impl as far as it goes first.

If you DO need to replace it, the abstract surface is the boundary. Nothing in `SeinARTSCoreEntity`, `SeinARTSFramework`, or `SeinARTSEditor` should ever need to change.
