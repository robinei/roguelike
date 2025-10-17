#include "events.h"

void entity_event_take_action_idle(EntityIndex entity) {
  // TODO
  entity_push_goal(entity, (Goal){
                               .type = GOAL_KILL,
                           });
}
