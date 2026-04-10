# Fixed-Point Math

All simulation math in SeinARTS uses `FFixedPoint` — a 32.32 fixed-point number format that guarantees identical results on every platform.

## Why Not Floats?

IEEE 754 floating-point (`float`, `double`) is non-deterministic across:

- **CPU architectures** — x86 vs. ARM can produce different rounding
- **Compiler settings** — `/fp:fast` vs. `/fp:strict` changes results
- **Optimization levels** — Debug vs. Release may fuse/reorder operations
- **Instruction sets** — SSE vs. AVX can differ in intermediate precision

For a single-player game, this doesn't matter. For lockstep networking — where two clients must produce bit-identical state from the same inputs — it's fatal. A single bit of divergence compounds into a full desync within seconds.

## FFixedPoint (32.32)

`FFixedPoint` stores numbers as a 64-bit integer where:

- The upper 32 bits represent the integer part
- The lower 32 bits represent the fractional part

```
 Integer (32 bits)          Fractional (32 bits)
├────────────────────────┼────────────────────────┤
 SSSS SSSS ... SSSS SSSS  FFFF FFFF ... FFFF FFFF
```

### Range and Precision

| Property | Value |
|----------|-------|
| **Minimum value** | -2,147,483,648 |
| **Maximum value** | ~2,147,483,647.999999999 |
| **Precision** | ~0.00000000023 (2^-32) |
| **Storage** | 8 bytes |

This gives sub-nanometer precision for game-scale coordinates and a range far exceeding any RTS map.

### Operations

All standard math operations are implemented with deterministic overflow behavior:

| Operation | Syntax | Notes |
|-----------|--------|-------|
| Add | `A + B` | Straightforward integer add |
| Subtract | `A - B` | Straightforward integer subtract |
| Multiply | `A * B` | 128-bit intermediate, shift result |
| Divide | `A / B` | 128-bit intermediate for precision |
| Compare | `A < B`, `A == B`, etc. | Integer comparison |
| Negate | `-A` | Integer negate |

### Construction

```cpp
FFixedPoint A = FFixedPoint::FromInt(42);        // 42.0
FFixedPoint B = FFixedPoint::FromFloat(3.14f);   // Closest fixed-point approximation
FFixedPoint C = FFixedPoint::FromRaw(0x100000000LL);  // Exactly 1.0
```

!!! warning "FromFloat is for initialization only"
    `FromFloat()` converts a float to fixed-point. This is fine for setting initial values (in archetype defaults, for example). But never use it in per-tick sim code — the float input itself is non-deterministic.

### Conversion to Float

```cpp
float Display = MyFixedPoint.ToFloat();  // For render/UI use only
```

In Blueprint, use the BPFL helper: `SeinFixedToFloat(Value) → float`

## Fixed-Point Spatial Types

The framework provides full spatial math types built on `FFixedPoint`:

| Type | Equivalent | Description |
|------|-----------|-------------|
| `FFixedVector` | `FVector` | 3D vector (X, Y, Z as FFixedPoint) |
| `FFixedQuat` | `FQuat` | Quaternion rotation |
| `FFixedRotator` | `FRotator` | Euler angle rotation |
| `FFixedTransform` | `FTransform` | Position + rotation + scale |

### Vector Operations

All standard vector operations are available:

```cpp
FFixedVector A(FFixedPoint::FromInt(10), FFixedPoint::FromInt(0), FFixedPoint::FromInt(0));
FFixedVector B(FFixedPoint::FromInt(0), FFixedPoint::FromInt(10), FFixedPoint::FromInt(0));

FFixedVector Sum = A + B;
FFixedPoint Dot = FFixedVector::DotProduct(A, B);
FFixedVector Cross = FFixedVector::CrossProduct(A, B);
FFixedPoint Length = A.Length();
FFixedVector Normal = A.GetSafeNormal();
```

### Conversion to UE Types

```cpp
FVector UnrealVec = MyFixedVector.ToVector();  // Render-side only
```

In Blueprint: `SeinFixedVectorToVector(Value) → FVector`

## Deterministic PRNG

For random numbers in the simulation, use the deterministic PRNG:

```cpp
// From sim context:
FFixedPoint Random = SimContext.GetPRNG().NextFixedPoint();  // [0, 1)
int32 RandomInt = SimContext.GetPRNG().NextInt(100);          // [0, 100)
```

The PRNG is seeded identically on all clients and advances deterministically. Never use `FMath::Rand()`, `FMath::FRand()`, or C++ `<random>` in sim code.

## Blueprint Usage

All fixed-point types are `BlueprintType` — they appear in Blueprint as pins and variables. The math BPFL provides Blueprint-accessible operators:

| Node | Description |
|------|-------------|
| `SeinFixedAdd(A, B)` | Add two FFixedPoint values |
| `SeinFixedMultiply(A, B)` | Multiply two FFixedPoint values |
| `SeinFixedDivide(A, B)` | Divide two FFixedPoint values |
| `SeinFixedFromInt(Value)` | Create FFixedPoint from integer |
| `SeinFixedToFloat(Value)` | Convert to float (render only) |
| `SeinFixedCompare(A, B)` | Compare two values |

## Tips

!!! sim "Think in integers"
    Fixed-point is integer math with a conceptual decimal point. If you're comfortable with "health is 1000 meaning 10.00 HP with 2 decimal places," fixed-point is the same idea with 32 decimal places and hardware integer instructions doing the work.

!!! warning "Avoid float ↔ fixed round-trips in sim code"
    Every conversion between float and fixed-point loses precision. In the sim, work entirely in fixed-point. Only convert to float at the render boundary (UI display, actor positions, FX parameters).

!!! tip "Attribute resolution returns float"
    `GetResolvedAttribute()` and `GetBaseAttribute()` in the UI ViewModel return `float` — the conversion is already done for you. This is safe because you're on the render side when using ViewModels.
