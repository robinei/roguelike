#pragma once

#include "../common.h"

#define MAX_GOALS UINT16_MAX

enum {
  GOAL_NONE,

  // Level 0: Perpetual/Behavioral Goals (Bottom of Stack)
  // These typically never complete—they define the creature's purpose:
  GOAL_IDLE,
  GOAL_HUNT,
  GOAL_ROAM,

  // Level 1: Tactical Goals (Mid-Level)
  // Time-bounded objectives that complete:
  GOAL_KILL,

  // Level 2: Atomic Actions (Top of Stack)
  // Single-turn primitive actions:
  GOAL_MOVE,
  GOAL_ATTACK,
};

typedef uint16_t GoalType;

typedef uint16_t GoalIndex;

typedef struct {
  GoalType type;
  GoalIndex original_intent;
  GoalIndex next; // for chaining off entities as a stack
  bool is_finished;
  union {
    EntityHandle target_entity;
    Position target_position;
  };
} Goal;

typedef struct {
  Goal goals[MAX_GOALS];
  uint32_t goals_count;
  GoalIndex goals_freelist[MAX_GOALS];
  uint32_t goals_freelist_count;
} AIState;

GoalIndex aistate_push_goal(AIState *ai, Goal goal);

void aistate_free_goal(AIState *ai, GoalIndex index);

GoalIndex entity_push_goal(EntityIndex entity, Goal goal);

Goal *entity_peek_goal(EntityIndex entity);

bool entity_has_goal(EntityIndex entity);

void entity_pop_goal(EntityIndex entity);
