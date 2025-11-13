#pragma once

#include "../common.h" // IWYU pragma: keep

// Simple arena allocator with checkpoint/restore support
typedef struct {
  uint8_t *buffer;
  size_t capacity;
  size_t offset;
} Arena;

// Checkpoint for restoring arena state
typedef size_t ArenaCheckpoint;

// Allocate from arena (returns NULL if out of space)
static inline void *arena_alloc(Arena *arena, size_t size) {
  // Align to 8 bytes
  size = (size + 7) & ~7;

  if (arena->offset + size > arena->capacity) {
    assert(0 && "out of arena memory");
    return NULL; // Out of memory
  }

  void *ptr = arena->buffer + arena->offset;
  memset(ptr, 0, size);
  arena->offset += size;
  return ptr;
}

// Save current position
static inline ArenaCheckpoint arena_checkpoint(Arena *arena) {
  return arena->offset;
}

// Restore to checkpoint (frees everything allocated after checkpoint)
static inline void arena_restore(Arena *arena, ArenaCheckpoint checkpoint) {
  arena->offset = checkpoint;
}

// Reset arena to empty (equivalent to restoring to checkpoint 0)
static inline void arena_reset(Arena *arena) { arena->offset = 0; }
