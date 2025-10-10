#include "../game/render_api.h"
#include "../game/world.h"
#include "atlas_view.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>
#include <stdbool.h>
#include <stdio.h>

// Game API function pointers (loaded dynamically)
typedef void (*GameInitFunc)(WorldState *world);
typedef void (*GameFrameFunc)(WorldState *world, double dt);
typedef void (*GameRenderFunc)(WorldState *world, PlatformContext *platform);
typedef void (*GameInputFunc)(WorldState *world, InputCommand command);

typedef struct {
  void *lib_handle;
  SDL_Time lib_mtime;
  GameInitFunc game_init;
  GameFrameFunc game_frame;
  GameRenderFunc game_render;
  GameInputFunc game_input;
} GameAPI;

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Tile constants
#define TILE_SIZE 12
#define TILE_PADDING 1 // 1 pixel transparent border between tiles

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *atlas_texture;
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

  // Create windowed mode for development (1280x720)
  r->window_width = 1280;
  r->window_height = 720;

  printf("Window size: %dx%d\n", r->window_width, r->window_height);

  // Create resizable windowed window with high DPI support
  r->window =
      SDL_CreateWindow("Roguelike", r->window_width, r->window_height,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  SDL_SetRenderDrawBlendMode(r->renderer,
                             SDL_BLENDMODE_BLEND); // Enable alpha blending

  // Load tileset with stb_image
  int channels;
  unsigned char *image_data = stbi_load("combined_tileset.png", &r->atlas_width,
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

  return true;
}

static void shutdown_renderer(Renderer *r) {
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

// Load or reload the game library
static bool load_game_api(GameAPI *api, const char *lib_path) {
  // Get current mtime
  SDL_PathInfo path_info;
  if (!SDL_GetPathInfo(lib_path, &path_info)) {
    fprintf(stderr, "Failed to stat %s: %s\n", lib_path, SDL_GetError());
    return false;
  }

  // Check if we need to reload
  if (api->lib_handle && path_info.modify_time == api->lib_mtime) {
    return true; // Already loaded and up to date
  }

  // Unload old library if present
  if (api->lib_handle) {
    printf("Reloading game library...\n");
    SDL_UnloadObject(api->lib_handle);
    api->lib_handle = NULL;
  }

  // Load the library
  api->lib_handle = SDL_LoadObject(lib_path);
  if (!api->lib_handle) {
    fprintf(stderr, "Failed to load %s: %s\n", lib_path, SDL_GetError());
    return false;
  }

  // Load function pointers
  api->game_init = (GameInitFunc)SDL_LoadFunction(api->lib_handle, "game_init");
  api->game_frame =
      (GameFrameFunc)SDL_LoadFunction(api->lib_handle, "game_frame");
  api->game_render =
      (GameRenderFunc)SDL_LoadFunction(api->lib_handle, "game_render");
  api->game_input =
      (GameInputFunc)SDL_LoadFunction(api->lib_handle, "game_input");

  if (!api->game_init || !api->game_frame || !api->game_render ||
      !api->game_input) {
    fprintf(stderr, "Failed to load game functions: %s\n", SDL_GetError());
    SDL_UnloadObject(api->lib_handle);
    api->lib_handle = NULL;
    return false;
  }

  api->lib_mtime = path_info.modify_time;
  printf("Game library loaded successfully (mtime: %lld)\n",
         (long long)api->lib_mtime);
  return true;
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
      // data[0] is unused (was atlas_id)
      int tile_index = data[1];
      int x = data[2];
      int y = data[3];
      int w = data[4];
      int h = data[5];

      // Use combined atlas texture
      SDL_Texture *texture = r->atlas_texture;

      // Calculate tile position in combined atlas (all tiles use same layout
      // with padding)
      int tile_x = tile_index % r->atlas_cols;
      int tile_y = tile_index / r->atlas_cols;

      // Atlas position with padding
      int atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
      int atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);

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

  // Load game library
  GameAPI game_api = {0};
  const char *lib_path = "./libgame.so";
  if (!load_game_api(&game_api, lib_path)) {
    return 1;
  }

  Renderer renderer = {
      .scale = 2, // Default 2x scaling
  };
  if (!init_renderer(&renderer)) {
    return 1;
  }

  // Initialize world
  WorldState world = {0};
  game_api.game_init(&world);

  bool running = true;
  SDL_Event event;

  // Timing for frames
  uint64_t last_frame_time = SDL_GetTicksNS();

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

      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        SDL_GetWindowSizeInPixels(renderer.window, &renderer.window_width,
                                  &renderer.window_height);
        recalculate_viewport(&renderer);
        printf("Window resized to %dx%d pixels (%dx%d tiles at %dx scale)\n",
               renderer.window_width, renderer.window_height,
               renderer.viewport_tiles_x, renderer.viewport_tiles_y,
               renderer.scale);
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
          game_api.game_input(&world, cmd);
        }
        break;

      default:
        break;
      }
    }

    // Check for hot-reload (every frame)
    load_game_api(&game_api, lib_path);

    // Call game_frame every frame
    double dt = (current_time - last_frame_time) /
                1000000000.0; // Convert ns to seconds
    last_frame_time = current_time;
    game_api.game_frame(&world, dt);

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
    game_api.game_render(&world, &platform);

    // Present
    SDL_RenderPresent(renderer.renderer);
  }

  // Cleanup
  if (game_api.lib_handle) {
    SDL_UnloadObject(game_api.lib_handle);
  }
  shutdown_renderer(&renderer);
  return 0;
}
