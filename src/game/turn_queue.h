#pragma once

#include "common.h"

// ============================================================================
// Turn queue (min-heap priority queue based on turn_schedule[e].delay)
// ============================================================================

typedef struct {
  int count;
  EntityHandle entities[MAX_ENTITIES];
} TurnQueue;

// Insert entity into turn queue with given delay (adds turn_schedule part)
void turn_queue_insert(EntityIndex entity, int16_t delay);

// Remove entity from turn queue (removes turn_schedule part)
void turn_queue_remove(EntityIndex entity);

// Add to entity's turn_schedule[e].delay and reprioritize in queue
void turn_queue_add_delay(EntityIndex entity, int16_t delta);

// Peek at next entity to act (lowest delay) without removing
EntityHandle turn_queue_peek();

// Remove and return next entity to act
EntityHandle turn_queue_pop();

// Debug: print turn queue in sorted order (non-destructive)
void turn_queue_debug_print();
