#pragma once

#include <stdbool.h>
#include <stdint.h>

// For freestanding WASM builds, use compiler builtins instead of libc
#ifdef __wasm__
#define NULL ((void *)0)
#define sqrtf __builtin_sqrt
#define sqrt __builtin_sqrt
#define sinf __builtin_sin
#define sin __builtin_sin
#define cosf __builtin_cos
#define cos __builtin_cos
#define atan2f __builtin_atan2
#define atan2 __builtin_atan2
#define memcpy __builtin_memcpy
#define memset __builtin_memset

// Log levels matching JavaScript console
typedef enum {
  LOG_DEBUG = 0,
  LOG_LOG = 1,
  LOG_INFO = 2,
  LOG_WARN = 3,
  LOG_ERROR = 4,
} LogLevel;

// Logging support - imported from JavaScript
extern void js_log(LogLevel level, const char *message);

// Helper macros for stringification
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Simple assert for WASM builds - log file:line and trap on failure
#define assert(x)                                                              \
  do {                                                                         \
    if (!(x)) {                                                                \
      js_log(LOG_ERROR,                                                        \
             __FILE__ ":" TOSTRING(__LINE__) ": Assertion failed: " #x);       \
      __builtin_trap();                                                        \
    }                                                                          \
  } while (0)
#else
#include <assert.h>
#include <math.h>
#include <string.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Quake 3 fast inverse square root
static inline float rsqrt(float number) {
  float x2 = number * 0.5f;
  float y = number;
  int32_t i = *(int32_t *)&y; // evil floating point bit level hacking
  i = 0x5f3759df - (i >> 1);  // what the fuck?
  y = *(float *)&i;
  y = y * (1.5f - (x2 * y * y)); // 1st iteration
  y = y * (1.5f - (x2 * y * y)); // 2nd iteration, this can be removed
  return y;
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

// ============================================================================
// Elementary entity support
// ============================================================================

#define MAX_ENTITIES 4096

typedef uint16_t EntityIndex;

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} EntityHandle;

static inline bool entity_handle_equals(EntityHandle a, EntityHandle b) {
  return *(uint32_t *)&a == *(uint32_t *)&b;
}

// ============================================================================
// Direction
// ============================================================================

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

static inline int dir_dx(Direction dir) {
  switch (dir) {
  case DIR_N:
    return 0;
  case DIR_NE:
    return 1;
  case DIR_E:
    return 1;
  case DIR_SE:
    return 1;
  case DIR_S:
    return 0;
  case DIR_SW:
    return -1;
  case DIR_W:
    return -1;
  case DIR_NW:
    return -1;
  default:
    return 0;
  }
}

static inline int dir_dy(Direction dir) {
  switch (dir) {
  case DIR_N:
    return -1;
  case DIR_NE:
    return -1;
  case DIR_E:
    return 0;
  case DIR_SE:
    return 1;
  case DIR_S:
    return 1;
  case DIR_SW:
    return 1;
  case DIR_W:
    return 0;
  case DIR_NW:
    return -1;
  default:
    return 0;
  }
}

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