#include "world.h"

// declare global WorldState
WorldState world;

void entityset_add(EntitySet *set, Entity handle) {
  uint32_t index = entity_to_index(handle);
  if (bitset_test(set->bitvec, index)) {
    return;
  }
  bitset_set(set->bitvec, index);
  set->entities[set->count++] = handle;
}

#define MAX_DEPTH 100

void entityset_expand_children(EntitySet *set) {
  // Track visited entities to avoid redundant parent chain walks
  uint64_t visited[BITVEC_WORDS];

  // Pre-populate visited with entities already in the set
  bitset_copy(visited, set->bitvec);

  // Scan all entities with parent component
  world_query(i, INC(parent)) {
    // Skip if already visited
    if (bitset_test(visited, i))
      continue;

    // Walk up parent chain, recording the path
    uint32_t path[MAX_DEPTH];
    uint32_t path_len = 0;
    path[path_len++] = i;
    bitset_set(visited, i);

    Entity current = world.parent[i];
    bool found = false;

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
      uint32_t current_idx = entity_to_index(current);

      // Check if this ancestor is in the set
      if (bitset_test(set->bitvec, current_idx)) {
        found = true;
        break;
      }

      // Check if already visited (meaning we've determined it's not a
      // descendant)
      if (bitset_test(visited, current_idx)) {
        break;
      }

      path[path_len++] = current_idx;
      bitset_set(visited, current_idx);

      if (!entity_has_component(current, parent))
        break;
      current = world.parent[current_idx];
    }

    // Add the entire path to set if found
    if (found) {
      for (uint32_t j = 0; j < path_len; j++) {
        entityset_add(set, entity_from_index(path[j]));
      }
    }
  }
}

#define DO_CLEAR_COMPONENT_BIT(type, name) CLEAR_COMPONENT_BIT(index, name);
#define DO_CLEAR_MARKER_BIT(name) CLEAR_COMPONENT_BIT(index, name);

void entityset_free(EntitySet *to_free) {
  entityset_expand_children(to_free);

  // Free all collected entities
  for (uint32_t i = 0; i < to_free->count; i++) {
    uint32_t index = entity_to_index(to_free->entities[i]);

    FOREACH_COMPONENT(DO_CLEAR_COMPONENT_BIT)
    FOREACH_MARKER(DO_CLEAR_MARKER_BIT)

    // Increment generation to invalidate the freed handle
    // Only return to freelist if generation hasn't maxed out
    if (world.generation[index] < UINT16_MAX) {
      world.generation[index]++;
      assert(world.freelist_count < MAX_ENTITIES);
      world.freelist[world.freelist_count++] = index;
    }
    // else: slot permanently retired at max generation
  }
}

Entity entity_alloc(void) {
  if (world.freelist_count > 0) {
    return entity_from_index(world.freelist[--world.freelist_count]);
  }
  assert(world.entity_count < MAX_ENTITIES);
  return entity_from_index(world.entity_count++);
}

void entity_free(Entity handle) {
  EntitySet to_free = {0};
  entityset_add(&to_free, handle);
  entityset_free(&to_free);
}
