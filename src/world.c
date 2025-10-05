#include "world.h"
#include <stdarg.h>
#include <stdio.h>

// declare global WorldState
WorldState world;

void output_message(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  // If buffer is full, drop oldest message
  if (world.messages_count == MESSAGE_COUNT_MAX) {
    world.messages_first = (world.messages_first + 1) % MESSAGE_COUNT_MAX;
  } else {
    world.messages_count++;
  }

  // Format message at next position
  uint32_t pos = (world.messages_first + world.messages_count - 1) % MESSAGE_COUNT_MAX;
  Message *msg = &world.messages[pos];
  msg->length = vsnprintf(msg->text, MESSAGE_LENGTH_MAX + 1, fmt, args);
  if (msg->length > MESSAGE_LENGTH_MAX) {
    msg->length = MESSAGE_LENGTH_MAX;
  }

  va_end(args);
}

void entityset_add(EntitySet *set, EntityIndex index) {
  if (bitset_test(set->bitset, index)) {
    return;
  }
  bitset_set(set->bitset, index);
  set->entities[set->count++] = index;
}

#define MAX_DEPTH 100

void entityset_expand_descendants(EntitySet *set) {
  // Track visited entities to avoid redundant parent chain walks
  uint64_t visited[BITSET_WORDS];

  // Pre-populate visited with entities already in the set
  bitset_copy(visited, set->bitset);

  // Scan all entities with parent component
  world_query(i, BITS(parent)) {
    // Skip if already visited
    if (bitset_test(visited, i))
      continue;

    // Walk up parent chain, recording the path
    uint32_t path[MAX_DEPTH];
    uint32_t path_len = 0;
    path[path_len++] = i;
    bitset_set(visited, i);

    EntityIndex current_idx = world.parent[i];
    bool found = false;

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
      // Check if this ancestor is in the set
      if (bitset_test(set->bitset, current_idx)) {
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

      if (!entity_has(current_idx, parent))
        break;
      current_idx = world.parent[current_idx];
    }

    // Add the entire path to set if found
    if (found) {
      for (uint32_t j = 0; j < path_len; j++) {
        entityset_add(set, path[j]);
      }
    }
  }
}

#define DO_CLEAR_COMPONENT_BIT(type, name) CLEAR_COMPONENT_BIT(index, name);
#define DO_CLEAR_MARKER_BIT(name) CLEAR_COMPONENT_BIT(index, name);

void entityset_free(EntitySet *to_free) {
  entityset_expand_descendants(to_free);

  // Free all collected entities
  for (uint32_t i = 0; i < to_free->count; i++) {
    EntityIndex index = to_free->entities[i];

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

EntityIndex entity_alloc(void) {
  if (world.freelist_count > 0) {
    return world.freelist[--world.freelist_count];
  }
  assert(world.entity_count < MAX_ENTITIES);
  return world.entity_count++;
}

void entity_free(EntityIndex index) {
  EntitySet to_free = {0};
  entityset_add(&to_free, index);
  entityset_free(&to_free);
}

bool entity_is_player(EntityIndex index) {
  return entity_handle_to_index(world.player) == index;
}
