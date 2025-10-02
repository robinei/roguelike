#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t _generation : 16;
  uint32_t _index : 16;
} Entity;

typedef struct {
  uint16_t x;
  uint16_t y;
} Position;

typedef struct {
  uint16_t max_health;
  uint16_t curr_health;
} Health;

enum {
  BODY_PART_HEAD,
  BODY_PART_TORSO,
  BODY_PART_ARM,
  BODY_PART_LEG,
  BODY_PART_WING,
  BODY_PART_TENTACLE,
  BODY_PART_TAIL,
};

typedef struct {
  uint8_t type : 4;
  uint8_t index : 4; // 0, 1, 2... for multiple parts of same type
} BodyPart;

#define FOREACH_COMPONENT(X)                                                   \
  X(Position, position)                                                        \
  X(Health, health)                                                            \
  X(BodyPart, body_part)                                                       \
  X(Entity, parent)

#define FOREACH_MARKER(X)                                                      \
  X(is_equipped)                                                               \
  X(is_inventory)                                                              \
  X(is_dead)
