# Custom Navigation Implementations

<span class="tag advanced">ADVANCED</span> The shipped A* impl handles small-to-medium RTS maps. For projects that need hierarchical pathing on huge maps, true 3D nav, navmesh-style cell adaptation, or integration with an existing pathing solution (Recast/Detour, custom waypoint graph, RecastNav, etc.), `USeinNavigation` is replaceable end-to-end.

## What you implement

Subclass `USeinNavigation`. Override the methods you need; the rest stay as the base's no-op / permissive defaults.

### Required (no useful default)

| Method | Purpose |
|---|---|
| `BeginBake(UWorld* World) → bool` | Kick off the bake. Sync or async; you choose. Return true if started. |
| `IsBaking() const → bool` | Bake-in-progress probe |
| `FindPath(const FSeinPathRequest&, FSeinPath& Out) const → bool` | The hot-path query. Returns true + populates `Out` on success. |
| `IsPassable(const FFixedVector& WorldPos) const → bool` | Static walkability test for a single point |
| `GetAssetClass() const → TSubclassOf<USeinNavigationAsset>` | Your concrete asset subclass — the framework instantiates this for you on bake-save |
| `LoadFromAsset(USeinNavigationAsset*) → void` | Hoist baked data into your runtime arrays |
| `HasRuntimeData() const → bool` | True iff your runtime state is populated |

### Strongly recommended

| Method | Why |
|---|---|
| `GetGroundHeightAt(const FFixedVector& XY, FFixedPoint& OutZ) const → bool` | Used by `USeinMoveToAction` per-tick to keep agents flush with terrain |
| `ProjectPointToNav(const FFixedVector& In, FFixedVector& Out) const → bool` | Snap arbitrary world points to the nearest walkable position |
| `SetDynamicBlockers(const TArray<FSeinDynamicBlocker>&) → void` | Receive per-tick blocker stamps. No-op default = your nav doesn't support runtime obstacle insertion. |

### Optional

| Method | Default behavior |
|---|---|
| `IsReachable(From, To, AgentTags) const → bool` | Falls back to `FindPath`. Override with a cheaper flood-fill component if FindPath cost is high (ability validation runs this per activation). |
| `RequestCancelBake() → void` | No-op. Override to cooperatively cancel async bakes. |
| `OnNavigationInitialized(UWorld*)` / `OnNavigationDeinitialized()` | Lifecycle hooks fired by the subsystem |
| `CollectDebugCellQuads`, `CollectDebugPathCells`, `CollectDebugBlockerCells` | Debug viz collectors. No-op default = no nav viz under `Sein.Nav.Show`. Subclasses without a grid concept can emit nothing. |
| `CustomizeVolumeDetails(IDetailLayoutBuilder&, ASeinNavVolume*)` | Editor extensibility hook. Add custom rows to the volume details panel below the framework's bake button. No-op default. |

## What you DON'T have to implement

- **Volume actor**: keep using `ASeinNavVolume`. It's just bake bounds + a polymorphic `BakedAsset` reference. Your custom asset class slots in via `GetAssetClass()`.
- **Subsystem**: `USeinNavigationSubsystem` instantiates whatever class plugin settings point at. It doesn't know what a "grid" is.
- **Move action**: `USeinMoveToAction` consumes `USeinNavigation::FindPath` only. Works against any impl.
- **Locomotion** (steering): `USeinMovement` and its 4 shipped subclasses live in `SeinARTSMovement` and consume nav through `IsPassable` + `GetGroundHeightAt`. They work against any impl.
- **Bake button**: `FSeinNavVolumeDetails` calls into your `BeginBake` via the abstract base. Designers see the same button.
- **Cross-module delegate**: `USeinNavigationSubsystem::BindSimDelegates` wires `USeinWorldSubsystem::PathableTargetResolver` to your `IsReachable`. Sim ability validation works against any impl.

## Build dependencies for your custom module

If you ship your impl as a separate plugin module:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "GameplayTags",
    "SeinARTSCore",          // FFixedPoint, FFixedVector
    "SeinARTSCoreEntity",    // FSeinEntityHandle, USeinWorldSubsystem (only if you reach into sim state)
    "SeinARTSNavigation"     // The abstract base + FSeinPath types + ASeinNavVolume
});

// Editor-only: your bake pipeline
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

After your subclass is registered (UCLASS, UBT picks it up on next compile):

**Project Settings → Plugins → SeinARTS → Navigation → Navigation Class**

The dropdown filters to `USeinNavigation` subclasses via `MetaClass = "/Script/SeinARTSNavigation.SeinNavigation"`. Pick yours.

The subsystem reads this on Initialize. If empty or stale, it falls back to `USeinNavAStar`.

## A minimal skeleton

```cpp
// MyNav.h
#include "SeinNavigation.h"
#include "MyNav.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "My Custom Nav"))
class MYMODULE_API UMyNav : public USeinNavigation
{
    GENERATED_BODY()
public:
    virtual TSubclassOf<USeinNavigationAsset> GetAssetClass() const override;
    virtual bool BeginBake(UWorld* World) override;
    virtual bool IsBaking() const override { return bBaking; }
    virtual void LoadFromAsset(USeinNavigationAsset* Asset) override;
    virtual bool HasRuntimeData() const override { return /* your state populated */; }

    virtual bool FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const override;
    virtual bool IsPassable(const FFixedVector& WorldPos) const override;
    virtual bool GetGroundHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;

private:
    bool bBaking = false;
    // ... your impl-specific runtime state ...
};
```

```cpp
// MyNav.cpp
TSubclassOf<USeinNavigationAsset> UMyNav::GetAssetClass() const
{
    return UMyNavAsset::StaticClass();  // Your concrete asset subclass
}

bool UMyNav::BeginBake(UWorld* World)
{
    if (bBaking) return false;
    bBaking = true;

    // Walk ASeinNavVolume actors via TActorIterator<ASeinNavVolume>(World)
    // Run your bake. When done:
    //   - Save your asset to disk (editor-only)
    //   - Assign asset to every volume's BakedAsset
    //   - Call OnNavigationMutated.Broadcast() so debug viz rebuilds
    //   - bBaking = false

    return true;
}

// ... etc.
```

The framework's hot-path determinism contract (FFixedPoint everywhere on the sim path, no `FMath::`, no float comparisons) applies to your impl too. The sim is single-threaded and lockstep — clients producing different paths from the same inputs is a desync.

## Things to verify

After your impl is in plugin settings:

- [ ] `Sein.Nav.Show 1` toggles your debug viz (or shows nothing if you don't override the collectors — fine)
- [ ] Designer drops `ASeinNavVolume`, clicks "Bake Navigation", your `BeginBake` is called
- [ ] After bake, your runtime asset survives a level reload (volume's `BakedAsset` is still assigned + your `LoadFromAsset` re-populates)
- [ ] A unit with a Move ability successfully pathfinds in PIE
- [ ] Ability validation rejects unreachable targets (check the cross-module `PathableTargetResolver` is wired — it should be, automatically, via `USeinNavigationSubsystem::BindSimDelegates`)
- [ ] `FindPath` is fully `FFixedPoint` end-to-end (no float comparisons inside the search) — desync risk otherwise

## When NOT to replace it

If `USeinNavAStar` works for your project, replacing it is an opportunity cost. Custom nav is a multi-week investment with a long determinism tail (cross-arch float behavior, edge cases in your bake pipeline, integrating debug viz). Try to push the default impl as far as it goes first — that's part of why it exists.

If you DO need to replace it, the abstract surface is the boundary. Nothing in `SeinARTSCoreEntity`, `SeinARTSMovement`, `SeinARTSFramework`, or `SeinARTSEditor` should ever need to change.
