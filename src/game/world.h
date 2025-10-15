#pragma once

#include "ai/ai.h"
#include "common.h"
#include "map.h"
#include "particles.h"
#include "parts.h"
#include "turn_queue.h"
#include <stdint.h>

#define TURN_INTERVAL 100

// ============================================================================
// Visible message log
// ============================================================================

#define MESSAGE_LENGTH_MAX 511
#define MESSAGE_COUNT_MAX 100

typedef struct {
  int length;
  char text[MESSAGE_LENGTH_MAX + 1];
} Message;

void output_message(const char *fmt, ...);

// ============================================================================
// Action animation
// ============================================================================

typedef enum {
  ACTION_ANIM_NONE,
  ACTION_ANIM_MOVE,
  ACTION_ANIM_ATTACK,
} ActionAnimType;

typedef struct {
  ActionAnimType type;
  EntityHandle actor;
  double progress; // 0.0 to 1.0, updated by game_frame()
  union {
    struct {
      Position from; // Tile coordinates
      Position to;   // Tile coordinates
    } move;
    struct {
      EntityHandle target;
    } attack;
  };
} ActionAnim;

// ============================================================================
// World state
// ============================================================================

typedef enum {
  INPUT_CMD_NONE,

  INPUT_CMD_UP,
  INPUT_CMD_UP_RIGHT,
  INPUT_CMD_RIGHT,
  INPUT_CMD_DOWN_RIGHT,
  INPUT_CMD_DOWN,
  INPUT_CMD_DOWN_LEFT,
  INPUT_CMD_LEFT,
  INPUT_CMD_UP_LEFT,

  INPUT_CMD_PERIOD,
  INPUT_CMD_R,
  INPUT_CMD_D,
} InputCommand;

typedef struct {
  uint32_t count;

  uint16_t freelist[MAX_ENTITIES];
  uint32_t freelist_count;

  uint16_t generation[MAX_ENTITIES];

  EntityHandle turn; // special entity which only exists to be inserted
                     // into the turn queue at regular intervals, and for
                     // which per-turn login is performed when popped
  EntityHandle player;
} EntitiesState;

// ============================================================================
// World state
// ============================================================================

typedef struct {
  EntitiesState entities;
  PartsState parts;
  AIState ai;

  Message messages[MESSAGE_COUNT_MAX];
  uint32_t messages_count;
  uint32_t messages_first;

  TurnQueue turn_queue;

  // particle state
  float particle_time;
  uint32_t particle_count;
  Particle particles[MAX_PARTICLES];

  Map map;

  ActionAnim anim;

  InputCommand next_player_input; // Next input to execute for player

  uint64_t rng_state;

  // state for tracking ticks (which happen at 10Hz and is used for some real
  // time scheduling porposes)
  double tick_accumulator;
  uint64_t tick_counter;

  // FPS tracking
  double frame_time_accumulator;
  uint32_t frame_count;
  float fps;

  // Debug flags
  bool debug_show_light_values;
} WorldState;

extern WorldState *active_world;
#define WORLD (*active_world)
#define MAP(x, y) WORLD.map.cells[(y) * MAP_WIDTH_MAX + (x)]
#define PART(part, entity) WORLD.parts.part[entity]

// query all world entities using BITS with part names to form a bitwise
// expression on bitset words
// example: world_query(i, BITS(position) & ~BITS(IsDead))
#define world_query(index_var, ...)                                            \
  for (uint32_t index_var, _word_idx = 0,                                      \
                           _word_idx_max = WORLD.entities.count / 64;          \
       _word_idx <= _word_idx_max; _word_idx++)                                \
    for (uint64_t _word = __VA_ARGS__;                                         \
         _word && (index_var = _word_idx * 64 + __builtin_ctzll(_word), 1);    \
         _word &= _word - 1)

#define BITS(part) WORLD.parts.part##_bitset[_word_idx]

// ============================================================================
// Part management utils
// ============================================================================

#define SET_PART_BIT(part, entity_idx)                                         \
  bitset_set(WORLD.parts.part##_bitset, entity_idx)

#define CLEAR_PART_BIT(part, entity_idx)                                       \
  bitset_clear(WORLD.parts.part##_bitset, entity_idx)

#define HAS_PART(part, entity_idx)                                             \
  bitset_test(WORLD.parts.part##_bitset, entity_idx)

#define SET_PART(part, entity_idx, value)                                      \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    PART(part, index) = value;                                                 \
    SET_PART_BIT(part, index);                                                 \
  } while (0)

#define CLEAR_PART(part, entity_idx)                                           \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    CLEAR_PART_BIT(part, index);                                               \
    memset(&PART(part, index), 0, sizeof(PART(part, index)));                  \
  } while (0)

#define SET_MARK(mark, entity_idx) SET_PART_BIT(mark, entity_idx);

#define CLEAR_MARK(mark, entity_idx) CLEAR_PART_BIT(mark, entity_idx)

// ============================================================================
// EntityHandle
// ============================================================================

static inline EntityIndex entity_handle_to_index(EntityHandle handle) {
  assert(handle._index < WORLD.entities.count);
  assert(WORLD.entities.generation[handle._index] == handle._generation);
  return handle._index;
}

static inline EntityHandle entity_handle_from_index(EntityIndex index) {
  assert(index < WORLD.entities.count);
  return (EntityHandle){WORLD.entities.generation[index], index};
}

// ============================================================================
// EntitySet
// ============================================================================

typedef struct {
  // stores added entities both by setting their bit in the bit set, and by
  // adding the index to the entities array. the redundancy makes both testing
  // membership and iterating fast
  uint32_t count;
  uint64_t bitset[BITSET_WORDS];
  EntityIndex entities[MAX_ENTITIES];
} EntitySet;

// add an entity to the set (unless already added)
void entityset_add(EntitySet *set, EntityIndex index);

// recursively expand (add) children of already added entities
void entityset_expand_descendants(EntitySet *set);

// free all entities in the set. children will be recursively added to the set
// and freed too
void entityset_free(EntitySet *to_free);

// query the entities in the EntitySet using HAS with part names
// example: entityset_query(i, &set, HAS(position) && !HAS(IsDead))
#define entityset_query(index_var, set, ...)                                   \
  for (uint32_t _i = 0, index_var, _entity_idx;                                \
       _i < (set)->count &&                                                    \
       (index_var = _entity_idx = (set)->entities[_i], 1);                     \
       _i++)                                                                   \
    if (__VA_ARGS__)

#define HAS(part) HAS_PART(part, _entity_idx)

// ============================================================================
// Entity alloc / free and utils
// ============================================================================

EntityIndex entity_alloc(void);
void entity_free(EntityIndex index);

bool entity_is_player(EntityIndex index);
EntityIndex get_position_ancestor(EntityIndex entity);
EntityIndex get_attributes_ancestor(EntityIndex entity);
