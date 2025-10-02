# Roguelike ECS Project

A simple, efficient Entity-Component-System implementation in C99 designed for game development.

## Project Overview

This is a sparse array-based ECS that prioritizes simplicity and performance over complex archetype systems. It trades some memory efficiency for development velocity and cache-friendly component access.

## Design Philosophy

- **Sparse arrays + bitsets** instead of archetypes or hash tables
- **Predictable performance** - no hash table cache misses or archetype migrations
- **Simple debugging** - components are exactly where you expect them (`world.position[entity_id]`)
- **Fast random access** - perfect for gameplay code that needs specific entities
- **Minimal complexity** - easier to understand and modify than complex ECS libraries

## Key Features

### Entity Management
- **Generational handles** - `Entity` prevents use-after-free bugs
- **Entity freelist** - reuses entity slots to keep indices dense
- **Safe handle validation** - `entity_to_index()` checks generation and bounds

### Component System
- **Bitset queries** - efficient iteration over entities with specific components
- **1-5 component queries** supported with same syntax
- **X-macro component registration** - single source of truth for all components
- **Automatic cleanup** - freeing entities clears all component bits

### Query Interface
```c
world_query(entity_id, (position, health)) {
    // Iterate over all entities with both position and health components
    uint16_t x = world.position[entity_id].x;
    uint16_t hp = world.health[entity_id].curr_health;
}
```

### Component Management
```c
Entity player = entity_alloc();
entity_add_component(player, position, (Position){10, 20});
entity_add_component(player, health, (Health){100, 100});
entity_remove_component(player, health);
entity_free(player);
```

## Memory Layout

For a roguelike with ~1000 entities and ~10 component types:
- **Entity overhead**: ~1000 × 4 bytes = 4KB (handles + generation)
- **Component arrays**: ~1000 × 8 bytes × 10 = 80KB
- **Bitsets**: 10 × (4096/64) × 8 bytes = ~5KB
- **Total**: ~90KB (negligible compared to game assets)

## Performance Characteristics

- **Component access**: O(1) array lookup
- **Entity allocation**: O(1) with freelist
- **Query iteration**: O(populated_entities) via bitset scanning
- **Memory usage**: Sparse but predictable

## Implementation Details

### Bitset Queries
- Uses `__builtin_ctzll()` for efficient bit scanning
- ANDs component bitsets to find entities with multiple components
- Only scans words up to `entity_count / 64` for efficiency

### Macro System
- **world_query**: Two nested `for` loops with entity ID calculation
- **X-macros**: `FOREACH_COMPONENT(X)` generates struct fields and cleanup code
- **Component dispatch**: Handles 1-5 components with same interface

### Build System
- **C99 compliant** with standard library only
- **Recursive source discovery** - automatically finds `.c` files in subdirectories
- **Dependency tracking** - uses GCC's `-MMD -MP` for header dependencies
- **Clean build artifacts** - all outputs go to `build/` directory

## Usage Example

```c
// Allocate entities
Entity player = entity_alloc();
Entity enemy = entity_alloc();

// Add components
entity_add_component(player, position, (Position){0, 0});
entity_add_component(player, health, (Health){100, 100});
entity_add_component(enemy, position, (Position){10, 10});
entity_add_component(enemy, health, (Health){50, 50});

// System: move all entities with position
world_query(i, (position)) {
    world.position[i].x += 1;
}

// System: damage entities with health
world_query(i, (health)) {
    if (world.health[i].curr_health > 0) {
        world.health[i].curr_health -= 10;
    }
}

// Query entities with multiple components
world_query(i, (position, health)) {
    // Handle entities that have both components
    if (world.health[i].curr_health <= 0) {
        Entity handle = entity_from_index(i);
        entity_remove_component(handle, health); // Entity "dies"
    }
}
```

## Adding New Components

1. Define the component struct
2. Add to `FOREACH_COMPONENT` macro
3. Recompile - everything else is automatic

```c
typedef struct {
    uint16_t dx, dy;
} Velocity;

#define FOREACH_COMPONENT(X)  \
  X(Position, position)       \
  X(Health, health)          \
  X(Velocity, velocity)       // <- Just add this line
```

## Build Commands

```bash
make           # Build the project
make clean     # Clean build artifacts
./build/roguelike  # Run the executable
```

## Why This Approach?

This ECS design is ideal for:
- **Rapid prototyping** of game mechanics
- **Games with stable entity counts** (hundreds to low thousands)
- **Teams wanting ECS benefits without complexity**
- **Projects prioritizing development speed over memory optimization**

The "wasted" memory from sparse arrays is typically negligible in modern games, while the development velocity gains from simple, predictable code are substantial.