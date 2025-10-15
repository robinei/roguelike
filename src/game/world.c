#include "world.h"

#define PRNF_SUPPORT_FLOAT
#define PRNF_SUPPORT_DOUBLE
#define PRNF_SUPPORT_LONG_LONG
#define PRNF_ENG_PREC_DEFAULT 0
#define PRNF_FLOAT_PREC_DEFAULT 3
#define PRNF_COL_ALIGNMENT
#define PRNF_IMPLEMENTATION
#include "prnf.h"

// declare global WorldState pointer
WorldState *active_world;

void output_message(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  // If buffer is full, drop oldest message
  if (WORLD.messages_count == MESSAGE_COUNT_MAX) {
    WORLD.messages_first = (WORLD.messages_first + 1) % MESSAGE_COUNT_MAX;
  } else {
    WORLD.messages_count++;
  }

  // Format message at next position
  uint32_t pos =
      (WORLD.messages_first + WORLD.messages_count - 1) % MESSAGE_COUNT_MAX;
  Message *msg = &WORLD.messages[pos];
  msg->length = vsnprnf(msg->text, MESSAGE_LENGTH_MAX + 1, fmt, args);
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

  // Scan all entities with parent part
  world_query(i, BITS(Parent)) {
    // Skip if already visited
    if (bitset_test(visited, i))
      continue;

    // Walk up parent chain, recording the path
    uint32_t path[MAX_DEPTH];
    uint32_t path_len = 0;
    path[path_len++] = i;
    bitset_set(visited, i);

    EntityIndex current_idx = PART(Parent, i);
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

      if (!HAS_PART(Parent, current_idx))
        break;
      current_idx = PART(Parent, current_idx);
    }

    // Add the entire path to set if found
    if (found) {
      for (uint32_t j = 0; j < path_len; j++) {
        entityset_add(set, path[j]);
      }
    }
  }
}

void entityset_free(EntitySet *to_free) {
  entityset_expand_descendants(to_free);

  // Free all collected entities
  for (uint32_t i = 0; i < to_free->count; i++) {
    EntityIndex index = to_free->entities[i];

    // Remove from turn queue if present
    if (HAS_PART(TurnSchedule, index)) {
      turn_queue_remove(index);
    }

#define DO_CLEAR_PART_BIT(name, type) CLEAR_PART_BIT(name, index);
#define DO_CLEAR_MARK_BIT(name) CLEAR_PART_BIT(name, index);
    FOREACH_PART(DO_CLEAR_PART_BIT)
    FOREACH_MARK(DO_CLEAR_MARK_BIT)
#undef DO_CLEAR_PART_BIT
#undef DO_CLEAR_MARK_BIT

    // Increment generation to invalidate the freed handle
    // Only return to freelist if generation hasn't maxed out
    if (WORLD.entities.generation[index] < UINT16_MAX) {
      WORLD.entities.generation[index]++;
      assert(WORLD.entities.freelist_count < MAX_ENTITIES);
      WORLD.entities.freelist[WORLD.entities.freelist_count++] = index;
    }
    // else: slot permanently retired at max generation
  }
}

EntityIndex entity_alloc(void) {
  if (WORLD.entities.freelist_count > 0) {
    return WORLD.entities.freelist[--WORLD.entities.freelist_count];
  }
  assert(WORLD.entities.count < MAX_ENTITIES);
  return WORLD.entities.count++;
}

void entity_free(EntityIndex index) {
  EntitySet to_free = {0};
  entityset_add(&to_free, index);
  entityset_free(&to_free);
}

bool entity_is_player(EntityIndex index) {
  return entity_handle_to_index(WORLD.entities.player) == index;
}

EntityIndex get_position_ancestor(EntityIndex entity) {
  for (;;) {
    if (HAS_PART(Position, entity)) {
      return entity;
    }
    if (!HAS_PART(Parent, entity)) {
      return 0;
    }
    entity = PART(Parent, entity);
  }
}

EntityIndex get_attributes_ancestor(EntityIndex entity) {
  for (;;) {
    if (HAS_PART(Attributes, entity)) {
      return entity;
    }
    if (!HAS_PART(Parent, entity)) {
      return 0;
    }
    entity = PART(Parent, entity);
  }
}
