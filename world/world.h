#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "components.h"

#define MAX_ENTITIES 4096

// ============================================================================
// Bitset utilities (operate on BITVEC_WORDS-sized bitsets)
// ============================================================================

#define BITVEC_WORDS (MAX_ENTITIES / 64)

static inline void bitset_set(uint64_t *bitset, uint32_t index) {
  bitset[index / 64] |= (1ULL << (index % 64));
}

static inline void bitset_clear(uint64_t *bitset, uint32_t index) {
  bitset[index / 64] &= ~(1ULL << (index % 64));
}

static inline bool bitset_test(const uint64_t *bitset, uint32_t index) {
  return (bitset[index / 64] >> (index % 64)) & 1;
}

static inline void bitset_copy(uint64_t *dst, const uint64_t *src) {
  memcpy(dst, src, BITVEC_WORDS * sizeof(uint64_t));
}

// ============================================================================
// Component management utils
// ============================================================================

#define SET_COMPONENT_BIT(entity_idx, comp)                                    \
  bitset_set(world.comp##_bitvec, entity_idx)

#define CLEAR_COMPONENT_BIT(entity_idx, comp)                                  \
  bitset_clear(world.comp##_bitvec, entity_idx)

#define entity_has_component(handle, comp)                                     \
  bitset_test(world.comp##_bitvec, entity_to_index(handle))

#define entity_add_component(handle, comp, value)                              \
  do {                                                                         \
    uint32_t index = entity_to_index(handle);                                  \
    world.comp[index] = value;                                                 \
    SET_COMPONENT_BIT(index, comp);                                            \
  } while (0)

#define entity_remove_component(handle, comp)                                  \
  do {                                                                         \
    uint32_t index = entity_to_index(handle);                                  \
    CLEAR_COMPONENT_BIT(index, comp);                                          \
    memset(&world.comp[index], 0, sizeof(world.comp[index]));                  \
  } while (0)

#define entity_add_marker(handle, marker)                                      \
  do {                                                                         \
    uint32_t index = entity_to_index(handle);                                  \
    SET_COMPONENT_BIT(index, marker);                                          \
  } while (0)

#define entity_remove_marker(handle, marker)                                   \
  do {                                                                         \
    uint32_t index = entity_to_index(handle);                                  \
    CLEAR_COMPONENT_BIT(index, marker);                                        \
  } while (0)

// ============================================================================
// Map
// ============================================================================

#define MAP_WIDTH_MAX 1024
#define MAP_HEIGHT_MAX 1024

typedef struct {
  uint32_t width;
  uint32_t height;
  uint8_t flags[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
} Map;

// ============================================================================
// World state
// ============================================================================

#define DO_DECLARE_COMPONENT(type, name)                                       \
  uint64_t name##_bitvec[BITVEC_WORDS];                                        \
  type name[MAX_ENTITIES];

#define DO_DECLARE_MARKER(name) uint64_t name##_bitvec[BITVEC_WORDS];

typedef struct {
  uint32_t entity_count;

  uint16_t freelist[MAX_ENTITIES];
  uint32_t freelist_count;

  uint16_t generation[MAX_ENTITIES];

  FOREACH_COMPONENT(DO_DECLARE_COMPONENT)
  FOREACH_MARKER(DO_DECLARE_MARKER)

  Map map;
} WorldState;

extern WorldState world;

#define INC(comp) world.comp##_bitvec[_word_idx] &
#define EXC(comp) ~world.comp##_bitvec[_word_idx] &

// query all world entities using INC and EXC with component names
#define world_query(index_var, ...)                                            \
  for (uint32_t index_var, _word_idx = 0,                                      \
                           _word_idx_max = world.entity_count / 64;            \
       _word_idx <= _word_idx_max; _word_idx++)                                \
    for (uint64_t _word = (__VA_ARGS__ ~0ULL);                                 \
         _word && (index_var = _word_idx * 64 + __builtin_ctzll(_word), 1);    \
         _word &= _word - 1)

// ============================================================================
// Entity set
// ============================================================================

typedef struct {
  uint64_t bitvec[BITVEC_WORDS]; // purely for tracking membership in the set
  Entity entities[MAX_ENTITIES];
  uint32_t count; // number of added entities
} EntitySet;

// add an entity to the set (unless already added)
void entityset_add(EntitySet *set, Entity handle);

// recursively expand (add) children of already added entities
void entityset_expand_children(EntitySet *set);

// free all entities in the set. children will be recursively added to the set
// and freed too
void entityset_free(EntitySet *to_free);

#define HAS(comp) bitset_test(world.comp##_bitvec, _entity_idx) &&
#define NOT(comp) (!bitset_test(world.comp##_bitvec, _entity_idx)) &&

// query the entities in the EntitySet using HAS and NOT with component names
#define entityset_query(entity_var, set, ...)                                  \
  for (uint32_t _i = 0, _entity_idx;                                           \
       _i < (set)->count &&                                                    \
       (_entity_idx = entity_to_index((set)->entities[_i]), 1);                \
       _i++)                                                                   \
    if (entity_var = _entity_idx, (__VA_ARGS__ 1))

// ============================================================================
// Entity alloc/free and handle utils
// ============================================================================

static inline uint32_t entity_to_index(Entity handle) {
  assert(handle._index < world.entity_count);
  assert(world.generation[handle._index] == handle._generation);
  return handle._index;
}

static inline Entity entity_from_index(uint32_t index) {
  assert(index < world.entity_count);
  return (Entity){world.generation[index], index};
}

static inline bool entity_eq(Entity a, Entity b) {
  return *(uint32_t *)&a == *(uint32_t *)&b;
}

Entity entity_alloc(void);
void entity_free(Entity handle);
