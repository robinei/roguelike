#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t x;
  uint16_t y;
} Position;

typedef struct {
  uint16_t max_health;
  uint16_t curr_health;
} Health;

#define FOREACH_COMPONENT(X)                                                   \
  X(Position, position)                                                        \
  X(Health, health)
