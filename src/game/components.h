#pragma once

#include "identity.h"

#define STR_DEFAULT 2
#define DEX_DEFAULT 2
#define WIL_DEFAULT 2
#define CON_DEFAULT 2

#define STR_MAX 15
#define DEX_MAX 15
#define WIL_MAX 15
#define CON_MAX 15

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
  MATERIAL_WOOD,
  MATERIAL_LEATHER,
  MATERIAL_BRONZE,
  MATERIAL_IRON,
  MATERIAL_STEEL,
  MATERIAL_MITHRIL,
  MATERIAL_ADAMANTINE,
};

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

enum {
  ROLL_NONE,
  ROLL_1D4,  // 1-4 (avg 2.5) - Dagger, dart
  ROLL_1D6,  // 1-6 (avg 3.5) - Shortsword, club
  ROLL_1D8,  // 1-8 (avg 4.5) - Longsword, spear
  ROLL_2D4,  // 2-8 (avg 5.0) - Dual daggers (consistent)
  ROLL_1D10, // 1-10 (avg 5.5) - Battleaxe, warhammer
  ROLL_1D12, // 1-12 (avg 6.5) - Greataxe (swingy)
  ROLL_2D6,  // 2-12 (avg 7.0) - Greatsword (consistent)
  ROLL_3D4,  // 3-12 (avg 7.5) - Magic missile
  ROLL_2D8,  // 2-16 (avg 9.0) - Heavy crossbow, powerful weapon
  ROLL_3D6,  // 3-18 (avg 10.5) - Fireball, standard AOE
  ROLL_2D10, // 2-20 (avg 11.0) - Giant club, huge weapon
  ROLL_2D12, // 2-24 (avg 13.0) - Legendary weapon (very swingy)
  ROLL_4D6,  // 4-24 (avg 14.0) - Dragon breath (consistent)
  ROLL_5D6,  // 5-30 (avg 17.5) - Massive spell/attack
  ROLL_6D6   // 6-36 (avg 21.0) - Ultimate/cataclysm
};

typedef struct {
  // damage rolls (roll enum values)
  uint8_t slash_damage : 4;
  uint8_t pierce_damage : 4;
  uint8_t blunt_damage : 4;
  uint8_t fire_damage : 4;
  uint8_t frost_damage : 4;
  uint8_t shock_damage : 4;
} DamageRolls;

typedef struct {
  // flat modifiers (-8 to 7)
  int8_t slash_modifier : 4;
  int8_t pierce_modifier : 4;
  int8_t blunt_modifier : 4;
  int8_t fire_modifier : 4;
  int8_t frost_modifier : 4;
  int8_t shock_modifier : 4;
} DamageModifiers;

typedef struct {
  uint8_t particle_type;
  uint8_t countdown_ticks;
} ParticleEmitter;

typedef struct {
  int16_t delay;
  uint16_t queue_index;
} TurnSchedule;

#define FOREACH_COMPONENT(X)                                                   \
  X(EntityIdentity, identity)                                                  \
  X(EntityIndex, parent)                                                       \
  X(TurnSchedule, turn_schedule)                                               \
  X(Position, position)                                                        \
  X(uint8_t, material)                                                         \
  X(Attributes, attributes)                                                    \
  X(AttributesModifier, attributes_modifier)                                   \
  X(uint8_t, health)                                                           \
  X(BodyPart, body_part)                                                       \
  X(ParticleEmitter, particle_emitter)

#define FOREACH_MARKER(X)                                                      \
  X(is_equipped)                                                               \
  X(is_inventory)                                                              \
  X(is_dead)
