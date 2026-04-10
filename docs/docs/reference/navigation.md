# Navigation Nodes

<span class="tag sim">SIM</span> Functions from the three navigation BPFLs. All pathfinding and movement is deterministic, using the fixed-point navigation grid.

## Pathfinding

From `USeinNavigationBPFL`:

### SeinRequestPath

```
SeinRequestPath(Pathfinder, Start, End, Requester, BlockedTerrainTags) → FSeinPath
```

Requests a path from start to end using A*/JPS on the navigation grid. The pathfinder uses fixed-point coordinates for determinism.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| Pathfinder | USeinPathfinder* | The pathfinder instance (get from world subsystem) |
| Start | FFixedVector | Start position |
| End | FFixedVector | Destination position |
| Requester | FSeinEntityHandle | Entity requesting the path (for size/clearance) |
| BlockedTerrainTags | FGameplayTagContainer | Additional terrain types to treat as blocked |

---

### SeinIsPathValid

```
SeinIsPathValid(Path) → bool
```

Returns `true` if the path was successfully computed. `false` means no path exists between the points.

---

### SeinGetPathWaypoints

```
SeinGetPathWaypoints(Path) → TArray<FFixedVector>
```

Returns the waypoint list. The entity should move through these in order.

---

### SeinGetPathCost

```
SeinGetPathCost(Path) → FFixedPoint
```

Total traversal cost considering terrain weights.

---

### SeinGetPathLength

```
SeinGetPathLength(Path) → int32
```

Number of waypoints in the path.

---

## Steering

From `USeinSteeringBPFL`. These are designer-composable movement primitives for building custom movement abilities.

### SeinMoveInDirection

```
SeinMoveInDirection(EntityHandle, Direction, Speed, DeltaTime) → void
```

Moves an entity one tick step in the given direction at the given speed. This is the fundamental movement primitive — abilities like `SeinMoveToAction` use this internally.

---

### SeinRotateToward

```
SeinRotateToward(EntityHandle, TargetDirection, TurnRate, DeltaTime) → bool
```

Rotates the entity toward a target direction at the given turn rate. Returns `true` when the entity is facing the target (within tolerance). Use for turret rotation, unit facing, and vehicle steering.

---

### SeinTeleportEntity

```
SeinTeleportEntity(EntityHandle, NewLocation) → void
```

Instantly moves an entity to a new position. Bypasses physics and collision. Use for garrison entry/exit, paradrop landing, or debug teleport.

---

### SeinApplyTurnRateLimit

```
SeinApplyTurnRateLimit(CurrentDirection, DesiredDirection, TurnRate, DeltaTime) → FFixedVector
```

Pure function — returns a new direction that is the current direction rotated toward the desired direction, limited by the turn rate. Use for vehicle-style movement where units can't turn instantly.

---

### SeinGetSeparationForce

```
SeinGetSeparationForce(MyPosition, NearbyPositions, Radius) → FFixedVector
```

Calculates a repulsion force pushing away from nearby entities. One of three flocking forces for group movement.

---

### SeinGetCohesionForce

```
SeinGetCohesionForce(MyPosition, GroupPositions) → FFixedVector
```

Calculates an attraction force toward the center of a group. Keeps formations together.

---

### SeinGetAlignmentForce

```
SeinGetAlignmentForce(MyHeading, NearbyHeadings) → FFixedVector
```

Calculates a force aligning the entity's heading with nearby entities. Creates uniform group movement direction.

### Composing Steering Behaviors

The three flocking forces are designed to be combined with weights:

```
FinalForce = Separation * 1.5
            + Cohesion * 0.8
            + Alignment * 1.0
            + PathFollowing * 2.0

Normalized → SeinMoveInDirection(Entity, FinalForce.Normalized, Speed, DT)
```

Designers can tune weights per unit type for different movement feel (tight infantry formations vs. loose vehicle columns).

---

## Terrain

From `USeinTerrainBPFL`. Query the navigation grid for terrain information.

### SeinGetTerrainTypeAt

```
SeinGetTerrainTypeAt(Grid, Location) → FGameplayTag
```

Returns the terrain type tag at a world position (e.g., `Terrain.Road`, `Terrain.Mud`, `Terrain.Water`). Use for movement speed modifiers, cover calculations, or visual indicators.

---

### SeinGetMovementCostAt

```
SeinGetMovementCostAt(Grid, Location) → uint8
```

Returns the movement cost at a grid cell. `0` = impassable, `1` = normal, higher values = slower terrain. The pathfinder uses these costs for optimal routing.

---

### SeinIsLocationWalkable

```
SeinIsLocationWalkable(Grid, Location) → bool
```

Returns `true` if the location has a movement cost > 0 (not blocked).

---

### SeinGetNearestWalkableLocation

```
SeinGetNearestWalkableLocation(Grid, Location, SearchRadius) → FFixedVector
```

Finds the nearest walkable position to a given point. Use when a click target or spawn point lands on impassable terrain.

---

### SeinWorldToGrid / SeinGridToWorld

```
SeinWorldToGrid(Grid, WorldLocation) → FIntPoint
SeinGridToWorld(Grid, GridPosition) → FFixedVector
```

Convert between world coordinates and grid cell coordinates. Useful for debug visualization and custom grid-based logic.
