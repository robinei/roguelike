#include "utils/print.h"
#include "world.h"

// ============================================================================
// Turn queue implementation (min-heap on turn_schedule[e].delay)
// ============================================================================

static inline void turn_queue_swap(uint16_t i, uint16_t j) {
  EntityHandle tmp = WORLD.turn_queue.entities[i];
  WORLD.turn_queue.entities[i] = WORLD.turn_queue.entities[j];
  WORLD.turn_queue.entities[j] = tmp;

  // Update stored heap indices (which heap position each entity is at)
  EntityIndex entity_i = entity_handle_to_index(WORLD.turn_queue.entities[i]);
  EntityIndex entity_j = entity_handle_to_index(WORLD.turn_queue.entities[j]);
  PART(TurnSchedule, entity_i).queue_index =
      i; // entity_i is now at heap position i
  PART(TurnSchedule, entity_j).queue_index =
      j; // entity_j is now at heap position j
}

static inline int turn_queue_compare(uint16_t a, uint16_t b) {
  EntityIndex idx_a = entity_handle_to_index(WORLD.turn_queue.entities[a]);
  EntityIndex idx_b = entity_handle_to_index(WORLD.turn_queue.entities[b]);
  int diff = PART(TurnSchedule, idx_a).delay - PART(TurnSchedule, idx_b).delay;
  return diff ? diff : idx_a - idx_b;
}

static void turn_queue_sift_up(uint16_t heap_index) {
  while (heap_index > 0) {
    uint16_t parent = (heap_index - 1) / 2;
    if (turn_queue_compare(heap_index, parent) >= 0)
      break;
    turn_queue_swap(heap_index, parent);
    heap_index = parent;
  }
}

static void turn_queue_sift_down(uint16_t heap_index) {
  while (1) {
    uint16_t left = 2 * heap_index + 1;
    uint16_t right = 2 * heap_index + 2;
    uint16_t smallest = heap_index;

    if (left < WORLD.turn_queue.count &&
        turn_queue_compare(left, smallest) < 0) {
      smallest = left;
    }
    if (right < WORLD.turn_queue.count &&
        turn_queue_compare(right, smallest) < 0) {
      smallest = right;
    }

    if (smallest == heap_index)
      break;

    turn_queue_swap(heap_index, smallest);
    heap_index = smallest;
  }
}

void turn_queue_insert(EntityIndex entity, int16_t delay) {
  assert(WORLD.turn_queue.count < MAX_ENTITIES);
  assert(!HAS_PART(TurnSchedule, entity)); // Must not already be in queue

  // Add part
  uint16_t heap_pos = WORLD.turn_queue.count;
  ADD_PART(TurnSchedule, entity,
           ((TurnSchedule){.delay = delay, .queue_index = heap_pos}));

  // Add to end of heap and sift up
  WORLD.turn_queue.entities[heap_pos] = entity_handle_from_index(entity);
  turn_queue_sift_up(heap_pos);
  WORLD.turn_queue.count++;
}

void turn_queue_remove(EntityIndex entity) {
  assert(HAS_PART(TurnSchedule, entity));

  uint16_t heap_index = PART(TurnSchedule, entity).queue_index;
  assert(heap_index < WORLD.turn_queue.count);

  // Remove part
  REMOVE_PART(TurnSchedule, entity);

  // Replace with last element
  WORLD.turn_queue.count--;
  if (heap_index < WORLD.turn_queue.count) {
    WORLD.turn_queue.entities[heap_index] =
        WORLD.turn_queue.entities[WORLD.turn_queue.count];
    EntityIndex moved_entity =
        entity_handle_to_index(WORLD.turn_queue.entities[heap_index]);
    PART(TurnSchedule, moved_entity).queue_index = heap_index;

    // Restore heap property - try both directions
    uint16_t original = heap_index;
    turn_queue_sift_up(heap_index);
    if (PART(TurnSchedule, moved_entity).queue_index == original) {
      turn_queue_sift_down(heap_index);
    }
  }
}

void turn_queue_add_delay(EntityIndex entity, int16_t delta) {
  assert(HAS_PART(TurnSchedule, entity));

  uint16_t heap_index = PART(TurnSchedule, entity).queue_index;
  PART(TurnSchedule, entity).delay += delta;

  // Reprioritize - try both directions
  uint16_t original = heap_index;
  turn_queue_sift_up(heap_index);
  if (PART(TurnSchedule, entity).queue_index == original) {
    turn_queue_sift_down(heap_index);
  }
}

EntityHandle turn_queue_peek() {
  assert(WORLD.turn_queue.count > 0);
  return WORLD.turn_queue.entities[0];
}

EntityHandle turn_queue_pop() {
  assert(WORLD.turn_queue.count > 0);

  EntityHandle result = WORLD.turn_queue.entities[0];
  EntityIndex entity = entity_handle_to_index(result);
  turn_queue_remove(entity);
  return result;
}

void turn_queue_debug_print() {
  // Clone queue and turn_schedule part array
  TurnQueue saved_queue = WORLD.turn_queue;
  TurnSchedule saved_schedule[MAX_ENTITIES];
  memcpy(saved_schedule, &PART(TurnSchedule, 0), sizeof(saved_schedule));

  PRINT(msg, 256, "Turn queue (");
  print_int(&msg, WORLD.turn_queue.count);
  print_str(&msg, "entities):");
  output_message(msg.data);

  while (WORLD.turn_queue.count > 0) {
    EntityHandle h = turn_queue_pop();
    EntityIndex e = entity_handle_to_index(h);

    PRINT(msg, 256, "  Entity ");
    print_int(&msg, e);
    print_str(&msg, ": delay=");
    print_int(&msg, PART(TurnSchedule, e).delay);
    output_message(msg.data);
  }

  // Restore queue and turn_schedule part array
  WORLD.turn_queue = saved_queue;
  memcpy(&PART(TurnSchedule, 0), saved_schedule, sizeof(saved_schedule));
}
