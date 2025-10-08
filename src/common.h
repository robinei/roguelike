#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_ENTITIES 4096

typedef uint16_t EntityIndex;

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} EntityHandle;

static inline bool entity_handle_equals(EntityHandle a, EntityHandle b) {
  return *(uint32_t *)&a == *(uint32_t *)&b;
}

static inline int clamp_int(int i, int min, int max) {
  if (i < min) {
    return min;
  }
  if (i > max) {
    return max;
  }
  return i;
}

typedef enum {
  DIR_N,
  DIR_NE,
  DIR_E,
  DIR_SE,
  DIR_S,
  DIR_SW,
  DIR_W,
  DIR_NW,
} Direction;

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