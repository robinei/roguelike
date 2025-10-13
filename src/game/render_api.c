#include "render_api.h"
#include "prnf.h"
#include <stdarg.h>

// Tile atlas constants
#define TILE_SIZE 12
#define TILE_PADDING 1

void geobuilder_init(GeometryBuilder *geom, RenderContext *ctx) {
  geom->count = 0;
  geom->ctx = ctx;
}

void geobuilder_clear(GeometryBuilder *geom) { geom->count = 0; }

void geobuilder_flush(GeometryBuilder *geom) {
  if (geom->count > 0) {
    geom->ctx->submit_geometry(geom->ctx->impl_data, geom->vertices,
                               geom->count);
    geom->count = 0;
  }
}

static void geobuilder_flush_if_full(GeometryBuilder *geom,
                                     int vertices_needed) {
  if (geom->count + vertices_needed > MAX_VERTICES) {
    geobuilder_flush(geom);
  }
}

static void geobuilder_vert(GeometryBuilder *geom, float x, float y,
                            Color color, float u, float v) {
  geobuilder_flush_if_full(geom, 1);

  Vertex *vert = &geom->vertices[geom->count++];
  vert->position[0] = x;
  vert->position[1] = y;
  vert->color[0] = color.r / 255.0f;
  vert->color[1] = color.g / 255.0f;
  vert->color[2] = color.b / 255.0f;
  vert->color[3] = color.a / 255.0f;
  vert->tex_coord[0] = u;
  vert->tex_coord[1] = v;
}

// Helper: push a quad (2 triangles = 6 vertices)
static void geobuilder_quad(GeometryBuilder *geom, float x0, float y0, float x1,
                            float y1, Color color, float u0, float v0, float u1,
                            float v1) {
  geobuilder_flush_if_full(geom, 6);

  // Triangle 1: top-left, top-right, bottom-left
  geobuilder_vert(geom, x0, y0, color, u0, v0);
  geobuilder_vert(geom, x1, y0, color, u1, v0);
  geobuilder_vert(geom, x0, y1, color, u0, v1);

  // Triangle 2: bottom-left, top-right, bottom-right
  geobuilder_vert(geom, x0, y1, color, u0, v1);
  geobuilder_vert(geom, x1, y0, color, u1, v0);
  geobuilder_vert(geom, x1, y1, color, u1, v1);
}

void geobuilder_tile(GeometryBuilder *geom, int tile_index, int x, int y) {
  RenderContext *ctx = geom->ctx;
  int tile_size = ctx->tile_size;

  // Calculate tile position in atlas (with padding)
  int atlas_cols =
      (ctx->atlas_width_px - TILE_PADDING) / (TILE_SIZE + TILE_PADDING);
  int tile_x = tile_index % atlas_cols;
  int tile_y = tile_index / atlas_cols;
  int atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
  int atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);

  // Calculate texture coordinates (0-1 range)
  float u0 = (float)atlas_x / ctx->atlas_width_px;
  float v0 = (float)atlas_y / ctx->atlas_height_px;
  float u1 = (float)(atlas_x + TILE_SIZE) / ctx->atlas_width_px;
  float v1 = (float)(atlas_y + TILE_SIZE) / ctx->atlas_height_px;

  // Screen coordinates
  float x0 = (float)x;
  float y0 = (float)y;
  float x1 = (float)(x + tile_size);
  float y1 = (float)(y + tile_size);

  // White color for textured quads (texture colors pass through)
  Color white = {255, 255, 255, 255};

  geobuilder_quad(geom, x0, y0, x1, y1, white, u0, v0, u1, v1);
}

void geobuilder_rect(GeometryBuilder *geom, int x, int y, int w, int h,
                     Color color) {
  RenderContext *ctx = geom->ctx;

  // Calculate white tile position in atlas
  int atlas_cols =
      (ctx->atlas_width_px - TILE_PADDING) / (TILE_SIZE + TILE_PADDING);
  int tile_x = WHITE_TILE_INDEX % atlas_cols;
  int tile_y = WHITE_TILE_INDEX / atlas_cols;
  int atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
  int atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);

  // Sample center pixel of white tile to avoid edge artifacts
  float u = (atlas_x + TILE_SIZE / 2.0f) / ctx->atlas_width_px;
  float v = (atlas_y + TILE_SIZE / 2.0f) / ctx->atlas_height_px;

  // Screen coordinates
  float x0 = (float)x;
  float y0 = (float)y;
  float x1 = (float)(x + w);
  float y1 = (float)(y + h);

  geobuilder_quad(geom, x0, y0, x1, y1, color, u, v, u, v);
}

void geobuilder_rect_colored(GeometryBuilder *geom, int x, int y, int w, int h,
                              Color tl, Color tr, Color bl, Color br) {
  RenderContext *ctx = geom->ctx;

  // Calculate white tile position in atlas
  int atlas_cols =
      (ctx->atlas_width_px - TILE_PADDING) / (TILE_SIZE + TILE_PADDING);
  int tile_x = WHITE_TILE_INDEX % atlas_cols;
  int tile_y = WHITE_TILE_INDEX / atlas_cols;
  int atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
  int atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);

  // Sample center pixel of white tile to avoid edge artifacts
  float u = (atlas_x + TILE_SIZE / 2.0f) / ctx->atlas_width_px;
  float v = (atlas_y + TILE_SIZE / 2.0f) / ctx->atlas_height_px;

  // Screen coordinates
  float x0 = (float)x;
  float y0 = (float)y;
  float x1 = (float)(x + w);
  float y1 = (float)(y + h);

  geobuilder_flush_if_full(geom, 6);

  // Triangle 1: top-left, top-right, bottom-left
  geobuilder_vert(geom, x0, y0, tl, u, v);
  geobuilder_vert(geom, x1, y0, tr, u, v);
  geobuilder_vert(geom, x0, y1, bl, u, v);

  // Triangle 2: bottom-left, top-right, bottom-right
  geobuilder_vert(geom, x0, y1, bl, u, v);
  geobuilder_vert(geom, x1, y0, tr, u, v);
  geobuilder_vert(geom, x1, y1, br, u, v);
}

void geobuilder_text(GeometryBuilder *geom, int x, int y, TextAlign align,
                     Color bg_color, const char *fmt, ...) {
  char text[256];
  va_list args;
  va_start(args, fmt);
  vsnprnf(text, sizeof(text), fmt, args);
  va_end(args);

  int tile_size = geom->ctx->tile_size;

  // Calculate text width
  int text_width = 0;
  for (const char *p = text; *p; p++) {
    text_width += tile_size;
  }

  // Adjust x for right alignment
  int draw_x = (align == TEXT_ALIGN_RIGHT) ? (x - text_width) : x;

  // Draw background if alpha > 0
  if (bg_color.a > 0) {
    geobuilder_rect(geom, draw_x, y, text_width, tile_size, bg_color);
  }

  // Draw text
  int char_x = draw_x;
  for (const char *p = text; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    geobuilder_tile(geom, FONT_BASE_INDEX + ch, char_x, y);
    char_x += tile_size;
  }
}
