# Custom Camera Provider

<span class="tag advanced">ADVANCED</span> The shipped `ASeinCameraPawn` works out of the box — its snapshot capture/restore covers pivot location, yaw, pitch, and zoom. If your project uses a marketplace camera asset (Fab), a custom C++ pawn, or a Blueprint-only pawn — and you want save-game round-tripping for view state — implement `ISeinSnapshotCameraProvider` on whichever UObject owns the camera.

## What you implement

The interface has exactly **two** functions, both `BlueprintNativeEvent` — implementable in C++ or in a Blueprint event graph.

| Function | When called | What to do |
|---|---|---|
| `CaptureCameraState(out FSeinCameraSnapshotData OutData)` | During `Sein.Net.DumpSnapshot` (and any future save-game flow), after the sim has been captured | Stamp current camera pose into `OutData`. Set `OutData.bHasState = true`. |
| `RestoreCameraState(in FSeinCameraSnapshotData Data)` | During `Sein.Net.LoadSnapshot` (and future load-game), after the sim is fully restored + the actor bridge is reconciled | Snap the camera to `Data`. Skip silently if `Data.bHasState` is false. |

`FSeinCameraSnapshotData` is the wire format. Both your provider and the snapshot system see the same struct; nothing else round-trips through it.

## What's in `FSeinCameraSnapshotData`

```cpp
USTRUCT(BlueprintType)
struct FSeinCameraSnapshotData
{
    bool      bHasState     = false;             // true = restore should run
    FVector   PivotLocation = FVector::ZeroVector;
    float     Yaw           = 0.0f;
    float     Pitch         = 0.0f;
    float     ZoomDistance  = 0.0f;
    TArray<uint8> CustomBytes;                   // free-form escape hatch
};
```

The five generic fields cover the standard pivot + spring-arm RTS camera model. If your camera is FOV-zoomed instead of arm-length-zoomed, orbital, cinematic-rig-driven, or has any state that doesn't map cleanly — serialize whatever you need into `CustomBytes` via `FMemoryWriter` / `FMemoryReader`.

The struct is `BlueprintType` so designers see clean named fields on the wildcard pin in BP graphs. The bytes blob just shows up as a `byte` array; you'd typically expose your own helper BP functions to serialize / deserialize it.

## Where the interface lookup runs

`USeinMatchBootstrapSubsystem` (Framework module) does the lookup at the moment of capture/restore. It walks:

1. `PlayerController->GetPawn()` — usually the camera pawn
2. `PlayerController->GetViewTarget()` — for spectator / cinematic view targets
3. `PlayerController` itself — projects that put camera state on the PC

First UObject implementing `ISeinSnapshotCameraProvider` wins. If none of the three implements it, snapshot camera state stays empty (`bHasState = false`) and restore is a silent no-op — your project just doesn't get camera in saves.

## A C++ skeleton

```cpp
// MyCameraPawn.h
#include "GameFramework/Pawn.h"
#include "Simulation/SeinSnapshotCameraProvider.h"
#include "MyCameraPawn.generated.h"

UCLASS()
class MYMODULE_API AMyCameraPawn : public APawn, public ISeinSnapshotCameraProvider
{
    GENERATED_BODY()
public:
    // ISeinSnapshotCameraProvider
    virtual void CaptureCameraState_Implementation(FSeinCameraSnapshotData& OutData) override;
    virtual void RestoreCameraState_Implementation(const FSeinCameraSnapshotData& Data) override;
};
```

```cpp
// MyCameraPawn.cpp
void AMyCameraPawn::CaptureCameraState_Implementation(FSeinCameraSnapshotData& OutData)
{
    OutData.bHasState     = true;
    OutData.PivotLocation = GetActorLocation();
    OutData.Yaw           = GetActorRotation().Yaw;
    OutData.Pitch         = MyCameraComponent ? MyCameraComponent->GetRelativeRotation().Pitch : 0.f;
    OutData.ZoomDistance  = MySpringArm ? MySpringArm->TargetArmLength : 0.f;

    // Camera-specific extras (FOV, target lock-on entity, etc.) — pack into bytes
    FMemoryWriter Writer(OutData.CustomBytes, /*bIsPersistent*/ true);
    Writer << CurrentFOV;
    Writer << bIsCinematicMode;
}

void AMyCameraPawn::RestoreCameraState_Implementation(const FSeinCameraSnapshotData& Data)
{
    if (!Data.bHasState) return;

    SetActorLocation(Data.PivotLocation);
    FRotator Rot = GetActorRotation();
    Rot.Yaw = Data.Yaw;
    SetActorRotation(Rot);

    if (MySpringArm)
    {
        FRotator ArmRot = MySpringArm->GetRelativeRotation();
        ArmRot.Pitch = Data.Pitch;
        MySpringArm->SetRelativeRotation(ArmRot);
        MySpringArm->TargetArmLength = Data.ZoomDistance;
    }

    if (Data.CustomBytes.Num() > 0)
    {
        FMemoryReader Reader(Data.CustomBytes, /*bIsPersistent*/ true);
        Reader << CurrentFOV;
        Reader << bIsCinematicMode;
    }

    // Important: skip any smooth-interp toward this state. The restore should
    // be instantaneous — lerping from current camera pose to the saved pose
    // feels wrong for a save-load.
}
```

## Blueprint-only pawn

If your camera pawn is a pure Blueprint asset (very common for marketplace cameras):

1. Open the BP. **Class Settings → Interfaces → Add → Sein Snapshot Camera Provider**.
2. Implement two events that now appear in **My Blueprint → Interfaces**:
   - `Capture Camera State` — break out the `FSeinCameraSnapshotData` struct, fill its fields from your camera's BP variables, set `Has State` to true.
   - `Restore Camera State` — read the struct fields, drive your BP variables / `Set Actor Location` / `Set Spring Arm Length` / etc. Skip if `Has State` is false.

You can split your camera state across the named fields + `Custom Bytes`. The wildcard struct pin shows the named fields directly; for `CustomBytes` use a project-side BP helper that wraps `FMemoryWriter` / `FMemoryReader`.

## Build dependencies

If you ship your camera pawn as a separate plugin module, the interface header is in `SeinARTSCoreEntity`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "SeinARTSCoreEntity"   // ISeinSnapshotCameraProvider, FSeinCameraSnapshotData
});
```

You **don't** need to depend on `SeinARTSFramework` (which owns the camera pawn). The interface lives in CoreEntity precisely so projects can implement it without dragging in the gameplay-layer module.

## What you DON'T have to implement

- **The lookup** — `USeinMatchBootstrapSubsystem` already walks PC → pawn → view target → PC. As long as your provider lives on one of those, it gets found.
- **The dispatch** — `Execute_CaptureCameraState` / `Execute_RestoreCameraState` are auto-generated by UHT.
- **The snapshot field plumbing** — `FSeinCameraSnapshotData` is already a member of `FSeinWorldSnapshot`; you only fill it.
- **Networking** — camera state is local-only. Don't try to sync it.

## Things to verify

After your impl is wired up:

- [ ] `Sein.Net.DumpSnapshot` writes a file (check `Saved/Snapshots/`)
- [ ] Move your camera, then `Sein.Net.LoadSnapshot <file>` snaps it back to the captured pose — instantaneous, not lerped
- [ ] `Capture` sets `bHasState = true` (otherwise restore no-ops silently — easy to miss)
- [ ] If you use `CustomBytes`, the read order in `Restore` matches the write order in `Capture` — version it with a leading `int32` if you plan to break the format later
- [ ] `Sein.Net.DumpState` after a load shows the same StateHash as before the dump — confirms the sim restored cleanly underneath your camera (camera doesn't affect hash, but a broken restore would show up here first)

## When to use the generic fields vs. `CustomBytes`

| Camera shape | Use generic fields | Use CustomBytes |
|---|---|---|
| Pivot + spring arm + scroll-zoom (default) | All four | — |
| Pivot + spring arm + FOV-zoom | Pivot/Yaw/Pitch | FOV |
| Free-fly cinematic | PivotLocation/Yaw/Pitch | velocity, target keyframe, etc. |
| Orbital around a target entity | PivotLocation = target loc, Yaw, Pitch | distance, target entity handle as int |
| Top-down strategy with no rotation | PivotLocation, ZoomDistance | — |
| Anything weirder | Fill what makes sense | Everything else |

Designer-friendly named fields when they fit, escape hatch when they don't.

## When NOT to implement it

If your project doesn't need camera state in saves — don't. The interface is opt-in. Snapshot dump/load works fine without it; you just land back at whatever pose your pawn happens to have at restore time (typically the spawn pose). For a debug-only snapshot system that's often acceptable. For shipped save-games where the player expects to "resume where I was looking," implement it.
