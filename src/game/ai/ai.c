#include "ai.h"
#include "../events/events.h"
#include "../world.h"

GoalIndex aistate_alloc_goal(AIState *ai, GoalType type,
                             GoalIndex original_intent, GoalIndex next) {
  if (ai->goals_freelist_count > 0) {
    return ai->goals_freelist[--ai->goals_freelist_count];
  }
  if (ai->goals_count >= MAX_GOALS) {
    return 0;
  }
  GoalIndex goal = ai->goals_count++;
  ai->goals[goal] =
      (Goal){.type = type, .original_intent = original_intent, .next = next};
  return goal;
}

void aistate_free_goal(AIState *ai, GoalIndex goal) {
  if (goal == ai->goals_count - 1) {
    --ai->goals_count;
  } else {
    ai->goals_freelist[ai->goals_freelist_count++] = goal;
  }
  ai->goals[goal] = (Goal){};
}

GoalIndex entity_push_goal(EntityIndex entity, GoalType type,
                           GoalIndex original_intent) {
  GoalIndex prev = 0;
  if (HAS_PART(Goals, entity)) {
    prev = PART(Goals, entity);
  } else {
    ADD_PART(Goals, entity, 0);
  }
  GoalIndex goal = aistate_alloc_goal(&WORLD.ai, type, original_intent, prev);
  PART(Goals, entity) = goal;
  return goal;
}

Goal *entity_peek_goal(EntityIndex entity) {
  if (!HAS_PART(Goals, entity)) {
    return NULL;
  }
  GoalIndex goal = PART(Goals, entity);
  if (goal == 0) {
    return NULL;
  }
  return &WORLD.ai.goals[goal];
}

bool entity_has_goal(EntityIndex entity) {
  return entity_peek_goal(entity) != NULL;
}

void entity_pop_goal(EntityIndex entity) {
  assert(entity_has_goal(entity));
  GoalIndex goal = PART(Goals, entity);
  PART(Goals, entity) = WORLD.ai.goals[goal].next;
  aistate_free_goal(&WORLD.ai, goal);
}

void entity_update_ai(EntityIndex entity) {
  for (;;) {
    Goal *goal = entity_peek_goal(entity);
    if (!goal) {
      entity_push_goal(entity, GOAL_IDLE, 0);
      continue;
    }

    if (goal->is_finished) {
      entity_pop_goal(entity);
      continue;
    }

    // Take action!
    switch (goal->type) {
    case GOAL_NONE:
      break; // do nothing. don't finish
    case GOAL_IDLE:
      entity_event_take_action_idle(entity);
      break;
    }

    // Took action once. Done for now.
    break;
  }
}