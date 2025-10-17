#pragma once

#include "ai/ai.h"
#include "common.h"
#include "map.h"
#include "particles.h"
#include "parts.h"
#include "turn_queue.h"

#define TURN_INTERVAL 100

// ============================================================================
// Input commands
// ============================================================================

typedef enum {
  INPUT_CMD_NONE,

  INPUT_CMD_UP,
  INPUT_CMD_RIGHT,
  INPUT_CMD_DOWN,
  INPUT_CMD_LEFT,

  INPUT_CMD_PERIOD,
  INPUT_CMD_R,
  INPUT_CMD_D,
} InputCommand;

// ============================================================================
// Visible message log
// ============================================================================

#define MESSAGE_LENGTH_MAX 511
#define MESSAGE_COUNT_MAX 100

typedef struct {
  int length;
  char text[MESSAGE_LENGTH_MAX + 1];
} Message;

typedef struct {
  uint32_t first;
  uint32_t count;
  Message buffer[MESSAGE_COUNT_MAX];
} MessageState;

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
// Entities state
// ============================================================================

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
  MessageState messages;
  TurnQueue turn_queue;
  ParticlesState particles;
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
#define ENTITIES WORLD.entities
#define PART(part, entity) WORLD.parts.part[entity]
#define MAP(x, y) WORLD.map.cells[(y) * MAP_WIDTH_MAX + (x)]

// query all world entities using BITS with part names to form a bitwise
// expression on bitset words
// example: WORLD_QUERY(i, BITS(Position) & ~BITS(IsDead))
#define WORLD_QUERY(index_var, ...)                                            \
  for (uint32_t index_var, _word_idx = 0, _word_idx_max = ENTITIES.count / 64; \
       _word_idx <= _word_idx_max; _word_idx++)                                \
    for (uint64_t _word = __VA_ARGS__;                                         \
         _word && (index_var = _word_idx * 64 + __builtin_ctzll(_word), 1);    \
         _word &= _word - 1)

#define BITS(part) WORLD.parts.part##Bitset[_word_idx]

// ============================================================================
// Part management utils
// ============================================================================

#define ENABLE_PART(part, entity_idx)                                          \
  bitset_set(WORLD.parts.part##Bitset, entity_idx)

#define DISABLE_PART(part, entity_idx)                                         \
  bitset_clear(WORLD.parts.part##Bitset, entity_idx)

#define HAS_PART(part, entity_idx)                                             \
  bitset_test(WORLD.parts.part##Bitset, entity_idx)

#define ADD_PART(part, entity_idx, value)                                      \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    ENABLE_PART(part, index);                                                  \
    PART(part, index) = value;                                                 \
  } while (0)

#define REMOVE_PART(part, entity_idx)                                          \
  do {                                                                         \
    uint32_t index = entity_idx;                                               \
    DISABLE_PART(part, index);                                                 \
    memset(&PART(part, index), 0, sizeof(PART(part, index)));                  \
  } while (0)

// ============================================================================
// EntityHandle
// ============================================================================

static inline EntityIndex entity_handle_is_valid(EntityHandle handle) {
  assert(handle._index < ENTITIES.count);
  return ENTITIES.generation[handle._index] == handle._generation;
}

static inline EntityIndex entity_handle_to_index(EntityHandle handle) {
  assert(entity_handle_is_valid(handle));
  return handle._index;
}

static inline EntityHandle entity_handle_from_index(EntityIndex index) {
  assert(index < ENTITIES.count);
  return (EntityHandle){ENTITIES.generation[index], index};
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
// example: ENTITYSET_QUERY(i, &set, HAS(position) && !HAS(IsDead))
#define ENTITYSET_QUERY(index_var, set, ...)                                   \
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
bool entity_is_alive(EntityHandle index);
EntityIndex get_position_ancestor(EntityIndex entity);
EntityIndex get_attributes_ancestor(EntityIndex entity);
