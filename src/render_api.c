#include "render_api.h"

void cmdbuf_clear(CommandBuffer *buf) { buf->count = 0; }

void cmdbuf_flush(CommandBuffer *buf, PlatformContext *ctx) {
  if (buf->count > 0) {
    ctx->execute_render_commands(ctx->impl_data, buf);
    cmdbuf_clear(buf);
  }
}

static void cmdbuf_flush_if_full(CommandBuffer *buf, PlatformContext *ctx) {
  if (buf->count >= COMMAND_BUFFER_CAPACITY) {
    ctx->execute_render_commands(ctx->impl_data, buf);
    cmdbuf_clear(buf);
  }
}

void cmdbuf_tile(CommandBuffer *buf, PlatformContext *ctx, AtlasId atlas,
                 int tile_index, int x, int y, int w, int h) {
  cmdbuf_flush_if_full(buf, ctx);

  int idx = buf->count;
  buf->types[idx] = RENDER_CMD_TILE;
  buf->data[idx * 6 + 0] = atlas;
  buf->data[idx * 6 + 1] = tile_index;
  buf->data[idx * 6 + 2] = x;
  buf->data[idx * 6 + 3] = y;
  buf->data[idx * 6 + 4] = w;
  buf->data[idx * 6 + 5] = h;
  buf->count++;
}

void cmdbuf_rect(CommandBuffer *buf, PlatformContext *ctx, int x, int y, int w,
                 int h, uint32_t color) {
  cmdbuf_flush_if_full(buf, ctx);

  int idx = buf->count;
  buf->types[idx] = RENDER_CMD_RECT;
  buf->data[idx * 6 + 0] = x;
  buf->data[idx * 6 + 1] = y;
  buf->data[idx * 6 + 2] = w;
  buf->data[idx * 6 + 3] = h;
  buf->data[idx * 6 + 4] = (int32_t)color;
  buf->data[idx * 6 + 5] = 0; // Unused
  buf->count++;
}

void cmdbuf_line(CommandBuffer *buf, PlatformContext *ctx, int x0, int y0,
                 int x1, int y1, uint32_t color) {
  cmdbuf_flush_if_full(buf, ctx);

  int idx = buf->count;
  buf->types[idx] = RENDER_CMD_LINE;
  buf->data[idx * 6 + 0] = x0;
  buf->data[idx * 6 + 1] = y0;
  buf->data[idx * 6 + 2] = x1;
  buf->data[idx * 6 + 3] = y1;
  buf->data[idx * 6 + 4] = (int32_t)color;
  buf->data[idx * 6 + 5] = 0; // Unused
  buf->count++;
}
