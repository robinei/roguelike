#pragma once

#include <stdint.h>

// ============================================================================
// Tile constants - combined atlas layout
// ============================================================================

// Original tileset has 10300 tiles (indices 0-10299)
// Font glyphs start at index 10300 (256 glyphs from CP437)
// Last tile (10711) is white for colored rects
#define FONT_BASE_INDEX 10300
#define WHITE_TILE_INDEX 10711

typedef enum {
  TILE_FLOOR = 1042,
  TILE_WALL = 618,
  TILE_PLAYER = 113,
  TILE_DOOR = 206,
  // Add more as needed based on the tileset
} TileType;

// ============================================================================
// Vertex format (compatible with SDL_Vertex)
// ============================================================================

// Vertex format compatible with SDL_Vertex
// This exact layout matches SDL_Vertex so hosts can cast directly
typedef struct {
  float position[2];  // Screen position in pixels (x, y)
  float color[4];     // Vertex color (r, g, b, a) in 0-1 range
  float tex_coord[2]; // Texture coordinates (u, v) in 0-1 range
} Vertex;

typedef struct {
  uint8_t r, g, b, a;
} Color;

#define MAX_VERTICES 4096

// ============================================================================
// Platform context - host services available to the game
// ============================================================================

typedef struct {
  // Viewport configuration
  int viewport_width_px;  // Viewport width in pixels
  int viewport_height_px; // Viewport height in pixels
  int tile_size;          // Logical tile size (12x12 for this game)

  // Atlas dimensions for calculating texture coordinates
  int atlas_width_px;
  int atlas_height_px;

  // Rendering callback - submits vertices for drawing
  // Vertices form triangles (every 3 vertices = 1 triangle)
  // The combined texture atlas is implicitly bound
  // Buffer can be reused immediately after this returns
  void (*submit_geometry)(void *impl_data, const Vertex *vertices,
                          int vertex_count);

  // Future host services can be added here:
  // void (*play_sound)(void *impl_data, SoundId sound);
  // void (*log_message)(void *impl_data, const char *msg);
  // void (*save_data)(void *impl_data, const void *data, int size);

  // Implementation-specific data (SDL renderer, textures, JS context, etc.)
  void *impl_data;
} RenderContext;

// ============================================================================
// Geometry builder - builds vertex buffers for rendering
// ============================================================================

typedef struct {
  Vertex vertices[MAX_VERTICES];
  int count;
  RenderContext *ctx;
} GeometryBuilder;

// Initialize geometry builder with render context
void geobuilder_init(GeometryBuilder *geom, RenderContext *ctx);

// Clear vertex buffer
void geobuilder_clear(GeometryBuilder *geom);

// Flush accumulated vertices to the host
void geobuilder_flush(GeometryBuilder *geom);

// Push a textured quad (6 vertices = 2 triangles)
// tile_index is the tile index in the combined atlas
// Renders at tile_size × tile_size from the context
void geobuilder_tile(GeometryBuilder *geom, int tile_index, int x, int y);

// Push a colored rect (6 vertices using white tile center)
void geobuilder_rect(GeometryBuilder *geom, int x, int y, int w, int h,
                     Color color);

// Push a colored rect with per-vertex colors (6 vertices using white tile center)
// Colors are specified for each corner: top-left, top-right, bottom-left, bottom-right
void geobuilder_rect_colored(GeometryBuilder *geom, int x, int y, int w, int h,
                              Color tl, Color tr, Color bl, Color br);

// Text rendering alignment
typedef enum {
  TEXT_ALIGN_LEFT,
  TEXT_ALIGN_RIGHT,
} TextAlign;

// Draw formatted text with optional background and scale
// If bg_color alpha is 0, no background is drawn
// scale: 1.0 = tile_size, 0.5 = half size, etc.
void geobuilder_text(GeometryBuilder *geom, int x, int y, float scale,
                     TextAlign align, Color bg_color, const char *fmt, ...);
