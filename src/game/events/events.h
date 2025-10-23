#pragma once

#include "../ai/ai.h"

void on_entity_free(EntityIndex entity);
void on_entity_pack(EntityIndex entity);
void on_entity_unpacked(EntityIndex entity);

void entity_event_take_action_idle(EntityIndex entity);
void entity_event_take_action_murder(EntityIndex entity, Goal *goal);
