#pragma once

#include "api.h" // IWYU pragma: keep

// ============================================================================
// "Runtime" support
// ============================================================================

#ifdef __wasm__

// For freestanding WASM builds, use compiler builtins instead of libc
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

// Simple assert for WASM builds - log file:line and trap on failure
#define assert(x)                                                              \
  do {                                                                         \
    if (!(x)) {                                                                \
      host_log(LOG_ERROR,                                                      \
               __FILE__ ":" TOSTRING(__LINE__) ": Assertion failed: " #x);     \
      __builtin_trap();                                                        \
    }                                                                          \
  } while (0)

#else

// when compiled for native we use libc for the following (only):
#include <assert.h> // IWYU pragma: keep
#include <math.h>   // IWYU pragma: keep
#include <string.h> // IWYU pragma: keep

#endif

// ============================================================================
// Common utilities
// ============================================================================

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

static inline int clamp_int(int x, int min, int max) {
  if (x < min) {
    return min;
  }
  if (x > max) {
    return max;
  }
  return x;
}

static inline float clamp_float(float x, float min, float max) {
  if (x < min) {
    return min;
  }
  if (x > max) {
    return max;
  }
  return x;
}

void output_message(const char *fmt, ...);

// ============================================================================
// Static max Map dimensions
// ============================================================================

// exposed here so you don't have to know about Map to declare buffers which
// correspond to what is used for Map
#define MAP_CHUNK_WIDTH 16
#define MAP_CHUNK_HEIGHT 16
#define MAP_CHUNK_WINDOW_X 3
#define MAP_CHUNK_WINDOW_Y 3
#define MAP_CHUNK_TOTAL_X 100
#define MAP_CHUNK_TOTAL_Y 70
#define MAP_WIDTH_MAX (MAP_CHUNK_WIDTH * MAP_CHUNK_WINDOW_X)
#define MAP_HEIGHT_MAX (MAP_CHUNK_HEIGHT * MAP_CHUNK_WINDOW_Y)

// ============================================================================
// Elementary entity support
// ============================================================================

#define MAX_ENTITIES 4096
#define ENTITY_BITSET_WORDS (MAX_ENTITIES / 64)

typedef uint16_t EntityIndex;

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} EntityHandle;

static inline bool entity_handle_equals(EntityHandle a, EntityHandle b) {
  return *(uint32_t *)&a == *(uint32_t *)&b;
}

// ============================================================================
// Common types
// ============================================================================

typedef struct {
  uint8_t r, g, b, a;
} Color;

typedef struct {
  uint16_t x;
  uint16_t y;
} Position;

enum {
  DIR_N,
  DIR_E,
  DIR_S,
  DIR_W,
};
typedef uint8_t Direction;

static inline int dir_dx(Direction dir) {
  switch (dir) {
  case DIR_N:
    return 0;
  case DIR_E:
    return 1;
  case DIR_S:
    return 0;
  case DIR_W:
    return -1;
  default:
    return 0;
  }
}

static inline int dir_dy(Direction dir) {
  switch (dir) {
  case DIR_N:
    return -1;
  case DIR_E:
    return 0;
  case DIR_S:
    return 1;
  case DIR_W:
    return 0;
  default:
    return 0;
  }
}

static inline Direction dir_opposite(Direction dir) {
  switch (dir) {
  case DIR_N:
    return DIR_S;
  case DIR_E:
    return DIR_W;
  case DIR_S:
    return DIR_N;
  case DIR_W:
    return DIR_E;
  default:
    return DIR_N;
  }
}
