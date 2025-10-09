#include "atlas_view.h"
#include "game.h"
#include "render_api.h"
#include "world.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>
#include <stdbool.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Tile constants
#define TILE_SIZE 12
#define TILE_PADDING 1 // 1 pixel transparent border between tiles

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *atlas_texture;
  SDL_Texture *font_texture;
  int atlas_width;
  int atlas_height;
  int atlas_cols;
  int atlas_rows;
  int window_width;
  int window_height;
  int viewport_tiles_x;
  int viewport_tiles_y;
  int message_scroll_offset; // How many lines scrolled up from bottom
  int scale;                 // Tile scaling factor (1, 2, or 3)
  int scaled_tile_size;      // TILE_SIZE * scale (cached)
} Renderer;

#define MESSAGE_DISPLAY_LINES 6 // Number of message lines to show

static void recalculate_viewport(Renderer *r) {
  r->scaled_tile_size = TILE_SIZE * r->scale;
  r->viewport_tiles_x =
      (r->window_width + r->scaled_tile_size - 1) / r->scaled_tile_size;
  r->viewport_tiles_y =
      (r->window_height + r->scaled_tile_size - 1) / r->scaled_tile_size;
}

static bool init_renderer(Renderer *r) {
  // Initialize SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  // Get native display resolution
  SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
  const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(display_id);
  if (!mode) {
    fprintf(stderr, "SDL_GetDesktopDisplayMode failed: %s\n", SDL_GetError());
    return false;
  }

  r->window_width = mode->w;
  r->window_height = mode->h;

  printf("Display resolution: %dx%d\n", r->window_width, r->window_height);

  // Create borderless fullscreen window with high DPI support
  r->window =
      SDL_CreateWindow("Roguelike", r->window_width, r->window_height,
                       SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!r->window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }

  // Get actual drawable size (may differ from window size with fractional
  // scaling)
  SDL_GetWindowSizeInPixels(r->window, &r->window_width, &r->window_height);
  printf("Actual pixel dimensions: %dx%d\n", r->window_width, r->window_height);

  // Create renderer
  r->renderer = SDL_CreateRenderer(r->window, NULL);
  if (!r->renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return false;
  }

  SDL_SetRenderVSync(r->renderer, 1); // Enable VSync

  // Load tileset with stb_image
  int channels;
  unsigned char *image_data =
      stbi_load("urizen_onebit_tileset__v2d0.png", &r->atlas_width,
                &r->atlas_height, &channels, 4);
  if (!image_data) {
    fprintf(stderr, "Failed to load tileset: %s\n", stbi_failure_reason());
    return false;
  }

  printf("Loaded tileset: %dx%d, %d channels\n", r->atlas_width,
         r->atlas_height, channels);

  // Calculate atlas grid dimensions
  // Formula: width = padding + (tile + padding) * cols
  // => cols = (width - padding) / (tile + padding)
  r->atlas_cols = (r->atlas_width - TILE_PADDING) / (TILE_SIZE + TILE_PADDING);
  r->atlas_rows = (r->atlas_height - TILE_PADDING) / (TILE_SIZE + TILE_PADDING);

  printf("Atlas grid: %d cols x %d rows = %d tiles\n", r->atlas_cols,
         r->atlas_rows, r->atlas_cols * r->atlas_rows);

  // Calculate viewport dimensions
  recalculate_viewport(r);
  printf("Viewport: %d x %d tiles (at %dx scale)\n", r->viewport_tiles_x,
         r->viewport_tiles_y, r->scale);

  // Create SDL texture from image data
  r->atlas_texture = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, r->atlas_width,
                                       r->atlas_height);
  if (!r->atlas_texture) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    stbi_image_free(image_data);
    return false;
  }

  // Upload image data to texture
  SDL_UpdateTexture(r->atlas_texture, NULL, image_data, r->atlas_width * 4);
  SDL_SetTextureScaleMode(r->atlas_texture, SDL_SCALEMODE_NEAREST);

  stbi_image_free(image_data);

  // Load CP437 font
  int font_channels;
  unsigned char *font_data = stbi_load("cp437_12x12.png", &font_channels,
                                       &font_channels, &font_channels, 4);
  if (!font_data) {
    fprintf(stderr, "Failed to load font: %s\n", stbi_failure_reason());
    return false;
  }

  // CP437 font is 16x16 grid of 12x12 glyphs
  r->font_texture = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, 16 * TILE_SIZE,
                                      16 * TILE_SIZE);
  if (!r->font_texture) {
    fprintf(stderr, "SDL_CreateTexture failed for font: %s\n", SDL_GetError());
    stbi_image_free(font_data);
    return false;
  }

  SDL_UpdateTexture(r->font_texture, NULL, font_data, 16 * TILE_SIZE * 4);
  SDL_SetTextureScaleMode(r->font_texture, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(r->font_texture, SDL_BLENDMODE_BLEND);

  stbi_image_free(font_data);

  return true;
}

static void shutdown_renderer(Renderer *r) {
  if (r->font_texture) {
    SDL_DestroyTexture(r->font_texture);
  }
  if (r->atlas_texture) {
    SDL_DestroyTexture(r->atlas_texture);
  }
  if (r->renderer) {
    SDL_DestroyRenderer(r->renderer);
  }
  if (r->window) {
    SDL_DestroyWindow(r->window);
  }
  SDL_Quit();
}

// Extract RGBA components from packed color
static inline void extract_rgba(uint32_t color, uint8_t *r, uint8_t *g,
                                uint8_t *b, uint8_t *a) {
  *r = (color >> 24) & 0xFF;
  *g = (color >> 16) & 0xFF;
  *b = (color >> 8) & 0xFF;
  *a = color & 0xFF;
}

// Execute a command buffer - callback for PlatformContext
static void execute_render_commands(void *impl_data,
                                    const CommandBuffer *buffer) {
  Renderer *r = (Renderer *)impl_data;

  for (int i = 0; i < buffer->count; i++) {
    const int32_t *data = &buffer->data[i * 6];

    switch (buffer->types[i]) {
    case RENDER_CMD_TILE: {
      AtlasId atlas_id = (AtlasId)data[0];
      int tile_index = data[1];
      int x = data[2];
      int y = data[3];
      int w = data[4];
      int h = data[5];

      // Select the appropriate texture
      SDL_Texture *texture =
          (atlas_id == ATLAS_TILES) ? r->atlas_texture : r->font_texture;

      // Calculate tile position in atlas
      int atlas_cols =
          (atlas_id == ATLAS_TILES) ? r->atlas_cols : 16; // Font is 16x16
      int tile_x = tile_index % atlas_cols;
      int tile_y = tile_index / atlas_cols;

      // Atlas position (with padding for tile atlas, no padding for font)
      int atlas_x, atlas_y;
      if (atlas_id == ATLAS_TILES) {
        atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
        atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);
      } else {
        atlas_x = tile_x * TILE_SIZE;
        atlas_y = tile_y * TILE_SIZE;
      }

      SDL_FRect src = {
          .x = (float)atlas_x,
          .y = (float)atlas_y,
          .w = TILE_SIZE,
          .h = TILE_SIZE,
      };

      SDL_FRect dst = {
          .x = (float)x,
          .y = (float)y,
          .w = (float)w,
          .h = (float)h,
      };

      SDL_RenderTexture(r->renderer, texture, &src, &dst);
      break;
    }

    case RENDER_CMD_RECT: {
      int x = data[0];
      int y = data[1];
      int w = data[2];
      int h = data[3];
      uint32_t color = (uint32_t)data[4];

      uint8_t red, green, blue, alpha;
      extract_rgba(color, &red, &green, &blue, &alpha);

      SDL_SetRenderDrawColor(r->renderer, red, green, blue, alpha);
      SDL_FRect rect = {
          .x = (float)x,
          .y = (float)y,
          .w = (float)w,
          .h = (float)h,
      };
      SDL_RenderFillRect(r->renderer, &rect);
      break;
    }

    case RENDER_CMD_LINE: {
      int x0 = data[0];
      int y0 = data[1];
      int x1 = data[2];
      int y1 = data[3];
      uint32_t color = (uint32_t)data[4];

      uint8_t red, green, blue, alpha;
      extract_rgba(color, &red, &green, &blue, &alpha);

      SDL_SetRenderDrawColor(r->renderer, red, green, blue, alpha);
      SDL_RenderLine(r->renderer, (float)x0, (float)y0, (float)x1, (float)y1);
      break;
    }
    }
  }
}

static InputCommand map_key_to_command(SDL_Keycode key) {
  switch (key) {
  case SDLK_UP:
  case SDLK_K:
    return INPUT_CMD_UP;
  case SDLK_DOWN:
  case SDLK_J:
    return INPUT_CMD_DOWN;
  case SDLK_LEFT:
  case SDLK_H:
    return INPUT_CMD_LEFT;
  case SDLK_RIGHT:
  case SDLK_L:
    return INPUT_CMD_RIGHT;
  case SDLK_Y:
    return INPUT_CMD_UP_LEFT;
  case SDLK_U:
    return INPUT_CMD_UP_RIGHT;
  case SDLK_B:
    return INPUT_CMD_DOWN_LEFT;
  case SDLK_N:
    return INPUT_CMD_DOWN_RIGHT;
  case SDLK_PERIOD:
    return INPUT_CMD_PERIOD;
  default:
    return INPUT_CMD_NONE; // Invalid
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  Renderer renderer = {
      .scale = 2, // Default 2x scaling
  };
  if (!init_renderer(&renderer)) {
    return 1;
  }

  // Initialize world
  WorldState world = {0};
  game_init(&world);

  bool running = true;
  SDL_Event event;

  // Timing for game_tick (10 ticks per second)
  uint64_t tick_counter = 0;
  uint64_t last_tick_time = SDL_GetTicksNS();
  const uint64_t tick_interval_ns = 100000000; // 100ms = 0.1s

  uint64_t last_frame_time = SDL_GetTicksNS();
  uint64_t frame_count = 0;
  uint64_t fps_report_time = SDL_GetTicksNS();

  while (running) {
    uint64_t current_time = SDL_GetTicksNS();

    // Event loop
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;

      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running = false;
        break;

      case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
          running = false;
          break;
        }

        // Toggle atlas viewer with 'A' key
        if (event.key.key == SDLK_A) {
          atlas_viewer_run(renderer.window, renderer.renderer,
                           renderer.atlas_texture, renderer.atlas_width,
                           renderer.atlas_height, renderer.atlas_cols,
                           renderer.atlas_rows, renderer.scale);
          break;
        }

        // Change scale factor with X key
        if (event.key.key == SDLK_X) {
          renderer.scale++;
          if (renderer.scale > 4)
            renderer.scale = 1;
          recalculate_viewport(&renderer);
          printf("Scale changed to %dx\n", renderer.scale);
          break;
        }

        // Scroll message log
        if (event.key.key == SDLK_PAGEUP) {
          renderer.message_scroll_offset++;
          int max_scroll = (int)world.messages_count - MESSAGE_DISPLAY_LINES;
          if (max_scroll < 0)
            max_scroll = 0;
          if (renderer.message_scroll_offset > max_scroll) {
            renderer.message_scroll_offset = max_scroll;
          }
          break;
        }
        if (event.key.key == SDLK_PAGEDOWN) {
          renderer.message_scroll_offset--;
          if (renderer.message_scroll_offset < 0) {
            renderer.message_scroll_offset = 0;
          }
          break;
        }

        InputCommand cmd = map_key_to_command(event.key.key);
        if (cmd != INPUT_CMD_NONE) {
          game_input(&world, cmd);
        }
        break;

      default:
        break;
      }
    }

    // Call game_tick at 10 Hz
    while (current_time - last_tick_time >= tick_interval_ns) {
      game_tick(&world, tick_counter);
      tick_counter++;
      last_tick_time += tick_interval_ns;

      // Print FPS every 10 ticks (once per second)
      if (tick_counter % 10 == 0) {
        double elapsed_sec = (current_time - fps_report_time) / 1000000000.0;
        double fps = frame_count / elapsed_sec;
        // printf("FPS: %.1f\n", fps);
        (void)fps; // Suppress unused warning
        frame_count = 0;
        fps_report_time = current_time;
      }
    }

    // Call game_frame every frame
    double dt = (current_time - last_frame_time) /
                1000000000.0; // Convert ns to seconds
    last_frame_time = current_time;
    game_frame(&world, dt);

    // Render
    // Clear to black
    SDL_SetRenderDrawColor(renderer.renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer.renderer);

    // Set up platform context
    PlatformContext platform = {
        .viewport_width_px = renderer.window_width,
        .viewport_height_px = renderer.window_height,
        .tile_size = renderer.scaled_tile_size,
        .execute_render_commands = execute_render_commands,
        .impl_data = &renderer,
    };

    // Call game render
    game_render(&world, &platform);

    // Present
    SDL_RenderPresent(renderer.renderer);
    frame_count++;
  }

  shutdown_renderer(&renderer);
  return 0;
}
