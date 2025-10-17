#include "ai.h"
#include "../events/events.h"
#include "../world.h"

GoalIndex aistate_push_goal(AIState *ai, Goal goal) {
  if (ai->goals_freelist_count > 0) {
    return ai->goals_freelist[--ai->goals_freelist_count];
  }
  if (ai->goals_count >= MAX_GOALS) {
    return 0;
  }
  GoalIndex index = ai->goals_count++;
  ai->goals[index] = goal;
  return index;
}

void aistate_free_goal(AIState *ai, GoalIndex index) {
  if (index == ai->goals_count - 1) {
    --ai->goals_count;
  } else {
    ai->goals_freelist[ai->goals_freelist_count++] = index;
  }
  ai->goals[index] = (Goal){};
}

GoalIndex entity_push_goal(EntityIndex entity, Goal goal) {
  if (HAS_PART(Goals, entity)) {
    goal.next = PART(Goals, entity);
  } else {
    goal.next = 0;
    ADD_PART(Goals, entity, 0);
  }
  return PART(Goals, entity) = aistate_push_goal(&WORLD.ai, goal);
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

void entity_take_action(EntityIndex entity) {
  if (!HAS_PART(Goals, entity)) {
    return;
  }

  for (;;) {
    Goal *goal = entity_peek_goal(entity);
    if (!goal) {
      entity_push_goal(entity, (Goal){.type = GOAL_IDLE});
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
    case GOAL_KILL:
      entity_event_take_action_murder(entity, goal);
      break;
    }

    // Took action once. Done for now.
    break;
  }
}