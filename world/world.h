#pragma once

#include "components.h"
#include "utils.h"
#include <assert.h>

#define MAX_ENTITIES 4096
#define BITVEC_WORDS (MAX_ENTITIES / 64)

#define DO_DECLARE_COMPONENT(type, name)                                       \
  uint64_t name##_bitvec[BITVEC_WORDS];                                        \
  type name[MAX_ENTITIES];

typedef struct {
  uint32_t entity_count;

  uint16_t freelist[MAX_ENTITIES];
  uint32_t freelist_count;

  uint16_t generation[MAX_ENTITIES];

  FOREACH_COMPONENT(DO_DECLARE_COMPONENT)
} WorldState;

extern WorldState world;

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} EntityHandle;

static inline uint32_t unwrap_handle(EntityHandle handle) {
  assert(handle._index < world.entity_count);
  assert(world.generation[handle._index] == handle._generation);
  return handle._index;
}

static inline EntityHandle make_handle(uint32_t index) {
  assert(index < world.entity_count);
  return (EntityHandle){world.generation[index], index};
}

EntityHandle allocate_entity(void);
void free_entity(EntityHandle handle);
