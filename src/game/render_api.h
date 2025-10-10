#pragma once

#include <stdint.h>

// ============================================================================
// Tile constants - combined atlas layout
// ============================================================================

// Original tileset has 10300 tiles (indices 0-10299)
// Font glyphs start at index 10300 (256 glyphs from CP437)
// Last tile (10711) is white for colored rects
#define FONT_BASE_INDEX 10300

typedef enum {
  TILE_FLOOR = 0,
  TILE_WALL = 1,
  TILE_PLAYER = 2884,
  TILE_DOOR = 50,
  // Add more as needed based on the tileset
} TileType;

// ============================================================================
// Render command buffer
// ============================================================================

typedef enum {
  RENDER_CMD_TILE, // Textured rect (atlas tiles, font glyphs, particles, etc.)
  RENDER_CMD_RECT, // Colored rect
  RENDER_CMD_LINE, // Colored line
} RenderCommandType;

#define COMMAND_BUFFER_CAPACITY 512

typedef struct {
  int count;
  uint8_t types[COMMAND_BUFFER_CAPACITY];
  int32_t data[COMMAND_BUFFER_CAPACITY * 6]; // Max 6 ints per command
} CommandBuffer;

// ============================================================================
// Platform context - host services available to the game
// ============================================================================

typedef struct {
  // Viewport configuration
  int viewport_width_px;  // Viewport width in pixels
  int viewport_height_px; // Viewport height in pixels
  int tile_size;          // Logical tile size (12x12 for this game)

  // Rendering callback - executes commands synchronously
  // All coordinates in command buffers are in pixels
  // Buffer can be reused immediately after this returns
  void (*execute_render_commands)(void *impl_data, const CommandBuffer *buffer);

  // Future host services can be added here:
  // void (*play_sound)(void *impl_data, SoundId sound);
  // void (*log_message)(void *impl_data, const char *msg);
  // void (*save_data)(void *impl_data, const void *data, int size);

  // Implementation-specific data (SDL renderer, textures, JS context, etc.)
  void *impl_data;
} PlatformContext;

// ============================================================================
// Command buffer helpers
// ============================================================================

// Color helper macro - RGBA8888 format
#define RGBA(r, g, b, a) ((uint32_t)((r) << 24 | (g) << 16 | (b) << 8 | (a)))

void cmdbuf_clear(CommandBuffer *buf);

void cmdbuf_flush(CommandBuffer *buf, PlatformContext *ctx);

// Draw textured rect from atlas (coordinates in pixels)
// TILE: tile_index, x, y, w, h (5 ints, using combined atlas)
void cmdbuf_tile(CommandBuffer *buf, PlatformContext *ctx, int tile_index,
                 int x, int y, int w, int h);

// Draw colored rect (coordinates in pixels)
// RECT: x, y, w, h, color (5 ints)
void cmdbuf_rect(CommandBuffer *buf, PlatformContext *ctx, int x, int y, int w,
                 int h, uint32_t color);

// Draw colored line (coordinates in pixels)
// LINE: x0, y0, x1, y1, color (5 ints)
void cmdbuf_line(CommandBuffer *buf, PlatformContext *ctx, int x0, int y0,
                 int x1, int y1, uint32_t color);
