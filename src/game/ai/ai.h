#pragma once

#include "../common.h"
#include <stdint.h>

#define MAX_GOALS UINT16_MAX

enum {
  GOAL_NONE,
  GOAL_IDLE,
  GOAL_MURDER,
  GOAL_MOVE,
};

typedef uint16_t GoalType;

typedef uint16_t GoalIndex;

typedef struct {
  GoalType type;
  GoalIndex original_intent;
  GoalIndex next; // for chaining off entities as a stack
  uint16_t is_finished;
} Goal;

typedef struct {
  Goal goals[MAX_GOALS];
  uint32_t goals_count;
  GoalIndex goals_freelist[MAX_GOALS];
  uint32_t goals_freelist_count;
} AIState;

GoalIndex aistate_alloc_goal(AIState *ai, GoalType type,
                             GoalIndex original_intent, GoalIndex next);
void aistate_free_goal(AIState *ai, GoalIndex goal);

GoalIndex entity_add_goal(EntityIndex entity, GoalType type,
                          GoalIndex original_intent);
Goal *entity_peek_goal(EntityIndex entity);
void entity_pop_goal(EntityIndex entity);
