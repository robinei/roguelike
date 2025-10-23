#include "world.h"
#include "common.h"
#include "events/events.h"
#include "parts.h"

#define PRNF_SUPPORT_FLOAT
#define PRNF_SUPPORT_DOUBLE
#define PRNF_SUPPORT_LONG_LONG
#define PRNF_ENG_PREC_DEFAULT 0
#define PRNF_FLOAT_PREC_DEFAULT 3
#define PRNF_COL_ALIGNMENT
#define PRNF_IMPLEMENTATION
#include "utils/prnf.h"

// declare global WorldState pointer
WorldState *active_world;

void output_message(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  // If buffer is full, drop oldest message
  if (WORLD.messages.count == MESSAGE_COUNT_MAX) {
    WORLD.messages.first = (WORLD.messages.first + 1) % MESSAGE_COUNT_MAX;
  } else {
    WORLD.messages.count++;
  }

  // Format message at next position
  uint32_t pos =
      (WORLD.messages.first + WORLD.messages.count - 1) % MESSAGE_COUNT_MAX;
  Message *msg = &WORLD.messages.buffer[pos];
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
  uint64_t visited[ENTITY_BITSET_WORDS];

  // Pre-populate visited with entities already in the set
  bitset_copy(visited, set->bitset);

  // Scan all entities with parent part
  WORLD_QUERY(i, BITS(Parent)) {
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

    on_entity_free(index);

#define DO_CLEAR_PART_BIT(name, type) DISABLE_PART(name, index);
#define DO_CLEAR_MARK_BIT(name) DISABLE_PART(name, index);
    FOREACH_PART(DO_CLEAR_PART_BIT)
    FOREACH_MARK(DO_CLEAR_MARK_BIT)
#undef DO_CLEAR_PART_BIT
#undef DO_CLEAR_MARK_BIT

    // Increment generation to invalidate the freed handle
    // Only return to freelist if generation hasn't maxed out
    if (ENTITIES.generation[index] < UINT16_MAX) {
      ENTITIES.generation[index]++;
      assert(ENTITIES.freelist_count < MAX_ENTITIES);
      ENTITIES.freelist[ENTITIES.freelist_count++] = index;
    }
    // else: slot permanently retired at max generation
  }
}

EntityIndex entity_alloc(void) {
  if (ENTITIES.freelist_count > 0) {
    return ENTITIES.freelist[--ENTITIES.freelist_count];
  }
  assert(ENTITIES.count < MAX_ENTITIES);
  return ENTITIES.count++;
}

void entity_free(EntityIndex index) {
  EntitySet to_free = {0};
  entityset_add(&to_free, index);
  entityset_free(&to_free);
}

bool entity_is_player(EntityIndex index) {
  return entity_handle_to_index(ENTITIES.player) == index;
}

bool entity_is_alive(EntityHandle handle) {
  return entity_handle_is_valid(handle) &&
         !HAS_PART(IsDead, entity_handle_to_index(handle));
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

void entity_pack(EntityIndex entity, ByteBuffer *buf) {
  on_entity_pack(entity);

  PartBitset part_bitset = {0};
  uint8_t *part_bitset_ptr = buf->data + buf->size;

  // initially write empty bitset to buffer
  bbuf_pack_bytes(buf, &part_bitset, sizeof(part_bitset), "part_bitset");

#define DO_PACK_MARK(name)                                                     \
  if (HAS_PART(name, entity)) {                                                \
    part_bitset_add(&part_bitset, PART_TYPE_##name);                           \
  }

#define DO_PACK_PART(name, type)                                               \
  if (HAS_PART(name, entity)) {                                                \
    part_bitset_add(&part_bitset, PART_TYPE_##name);                           \
    bbuf_pack_bytes(buf, &PART(name, entity), sizeof(PART(name, entity)),      \
                    "part_" #name);                                            \
  }

  FOREACH_MARK(DO_PACK_MARK)
  FOREACH_PART(DO_PACK_PART)

#undef DO_SET_MARK_BIT
#undef DO_PACK_PART

  assert(buf->size <= buf->capacity);

  // go back and overwrite empty bitset in buffer
  memcpy(part_bitset_ptr, &part_bitset, sizeof(part_bitset));
}

EntityIndex entity_unpack(ByteBuffer *buf) {
  EntityIndex entity = entity_alloc();

  PartBitset part_bitset;
  bbuf_unpack_bytes(buf, &part_bitset, sizeof(part_bitset), "part_bitset");

#define DU_UNPACK_MARK(name)                                                   \
  if (part_bitset_test(&part_bitset, PART_TYPE_##name)) {                      \
    ENABLE_PART(name, entity);                                                 \
  }

#define DU_UNPACK_PART(name, type)                                             \
  if (part_bitset_test(&part_bitset, PART_TYPE_##name)) {                      \
    ENABLE_PART(name, entity);                                                 \
    bbuf_unpack_bytes(buf, &PART(name, entity), sizeof(type), "part_" #name);  \
  }

  FOREACH_MARK(DU_UNPACK_MARK)
  FOREACH_PART(DU_UNPACK_PART)

#undef DO_SET_MARK_BIT
#undef DU_UNPACK_PART

  on_entity_unpacked(entity);

  return entity;
}
