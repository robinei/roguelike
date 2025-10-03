#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "components.h"

#define MAX_ENTITIES 4096

typedef uint16_t EntityIndex;

// ============================================================================
// Bitset utilities (operate on BITSET_WORDS-sized bitsets)
// ============================================================================

#define BITSET_WORDS (MAX_ENTITIES / 64)

static inline void bitset_set(uint64_t *bitset, EntityIndex index) {
  bitset[index / 64] |= (1ULL << (index % 64));
}

static inline void bitset_clear(uint64_t *bitset, EntityIndex index) {
  bitset[index / 64] &= ~(1ULL << (index % 64));
}

static inline bool bitset_test(const uint64_t *bitset, EntityIndex index) {
  return (bitset[index / 64] >> (index % 64)) & 1;
}

static inline void bitset_copy(uint64_t *dst, const uint64_t *src) {
  memcpy(dst, src, BITSET_WORDS * sizeof(uint64_t));
}

// ============================================================================
// Component management utils
// ============================================================================

#define SET_COMPONENT_BIT(entity_idx, comp)                                    \
  bitset_set(world.comp##_bitset, entity_idx)

#define CLEAR_COMPONENT_BIT(entity_idx, comp)                                  \
  bitset_clear(world.comp##_bitset, entity_idx)

#define entity_has(entity_idx, comp)                                           \
  bitset_test(world.comp##_bitset, entity_idx)

#define entity_add(entity_idx, comp, value)                                    \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    world.comp[index] = value;                                                 \
    SET_COMPONENT_BIT(index, comp);                                            \
  } while (0)

#define entity_remove(entity_idx, comp)                                        \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    CLEAR_COMPONENT_BIT(index, comp);                                          \
    memset(&world.comp[index], 0, sizeof(world.comp[index]));                  \
  } while (0)

#define entity_mark(entity_idx, marker)                                        \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    SET_COMPONENT_BIT(index, marker);                                          \
  } while (0)

#define entity_unmark(entity_idx, marker)                                      \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
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
  uint64_t name##_bitset[BITSET_WORDS];                                        \
  type name[MAX_ENTITIES];

#define DO_DECLARE_MARKER(name) uint64_t name##_bitset[BITSET_WORDS];

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

#define INC(comp) world.comp##_bitset[_word_idx] &
#define EXC(comp) ~world.comp##_bitset[_word_idx] &

// query all world entities using INC and EXC with component names
#define world_query(index_var, ...)                                            \
  for (uint32_t index_var, _word_idx = 0,                                      \
                           _word_idx_max = world.entity_count / 64;            \
       _word_idx <= _word_idx_max; _word_idx++)                                \
    for (uint64_t _word = (__VA_ARGS__ ~0ULL);                                 \
         _word && (index_var = _word_idx * 64 + __builtin_ctzll(_word), 1);    \
         _word &= _word - 1)

// ============================================================================
// EntityHandle set
// ============================================================================

typedef struct {
  uint32_t count;                // number of added entities
  uint64_t bitset[BITSET_WORDS]; // purely for tracking membership in the set
  EntityIndex entities[MAX_ENTITIES];
} EntitySet;

// add an entity to the set (unless already added)
void entityset_add(EntitySet *set, EntityIndex index);

// recursively expand (add) children of already added entities
void entityset_expand_descendants(EntitySet *set);

// free all entities in the set. children will be recursively added to the set
// and freed too
void entityset_free(EntitySet *to_free);

#define HAS(comp) bitset_test(world.comp##_bitset, _entity_idx) &&
#define NOT(comp) (!bitset_test(world.comp##_bitset, _entity_idx)) &&

// query the entities in the EntitySet using HAS and NOT with component names
#define entityset_query(index_var, set, ...)                                   \
  for (uint32_t _i = 0, index_var;                                             \
       _i < (set)->count && (index_var = (set)->entities[_i], 1); _i++)        \
    if (__VA_ARGS__ 1)

// ============================================================================
// EntityHandle
// ============================================================================

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} EntityHandle;

static inline EntityIndex entity_handle_to_index(EntityHandle handle) {
  assert(handle._index < world.entity_count);
  assert(world.generation[handle._index] == handle._generation);
  return handle._index;
}

static inline EntityHandle entity_handle_from_index(EntityIndex index) {
  assert(index < world.entity_count);
  return (EntityHandle){world.generation[index], index};
}

static inline bool entity_equals(EntityHandle a, EntityHandle b) {
  return *(uint32_t *)&a == *(uint32_t *)&b;
}

// ============================================================================
// Entity alloc / free
// ============================================================================

EntityIndex entity_alloc(void);
void entity_free(EntityIndex handle);
