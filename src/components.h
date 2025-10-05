#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STR_DEFAULT 2
#define DEX_DEFAULT 2
#define WIL_DEFAULT 2
#define CON_DEFAULT 2

#define HEALTH_FULL 100

typedef struct {
  uint16_t x;
  uint16_t y;
} Position;

// Base attributes (on character entity)
typedef struct {
  uint8_t str : 4;
  uint8_t dex : 4;
  uint8_t wil : 4;
  uint8_t con : 4;
} Attributes;

// Attributes modifier (on equipment/buff entities)
typedef struct {
  int8_t str : 4; // signed! can be negative
  int8_t dex : 4;
  int8_t wil : 4;
  int8_t con : 4;
} AttributesModifier;

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

typedef struct {
  uint8_t particle_type;
  uint8_t countdown_ticks;
} ParticleEmitter;

#define FOREACH_COMPONENT(X)                                                   \
  X(Position, position)                                                        \
  X(Attributes, attributes)                                                    \
  X(AttributesModifier, attributes_modifier)                                   \
  X(uint8_t, health)                                                           \
  X(BodyPart, body_part)                                                       \
  X(EntityIndex, parent)                                                       \
  X(ParticleEmitter, particle_emitter)

#define FOREACH_MARKER(X)                                                      \
  X(is_equipped)                                                               \
  X(is_inventory)                                                              \
  X(is_dead)
