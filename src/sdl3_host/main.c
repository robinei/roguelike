#include "../game/api.h"
#include "atlas_view.h"
#include "storage_file.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static StorageFile save_file;
static size_t state_memory_size;
static void *state_memory;
static uint64_t random_seed;

typedef struct {
  void *lib_handle;
  SDL_Time lib_mtime;

  GameSetHostFunctionsFn game_set_host_functions;
  GameGetMemorySizeFn game_get_memory_size;
  GameSetMemoryFn game_set_memory;
  GameInitFn game_init;
  GameInputFn game_input;
  GameFrameFn game_frame;
  GameRenderFn game_render;
  GameChunkStoredFn game_chunk_stored;
  GameChunkLoadedFn game_chunk_loaded;
} GameAPI;

static GameAPI game_api;

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

static Renderer renderer;

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
  case SDLK_PERIOD:
    return INPUT_CMD_PERIOD;
  case SDLK_D:
    return INPUT_CMD_D;
  default:
    return INPUT_CMD_NONE; // Invalid
  }
}

static HOST_LOG_SIG(do_log) {
  switch (level) {
  case LOG_DEBUG:
  case LOG_LOG:
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    break;
  case LOG_INFO:
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    break;
  case LOG_WARN:
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    break;
  case LOG_ERROR:
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    break;
  }
}

static HOST_SUBMIT_GEOMETRY_SIG(do_submit_geometry) {
  // Cast our Vertex format directly to SDL_Vertex
  // They have identical layout by design
  SDL_RenderGeometry(renderer.renderer, renderer.atlas_texture,
                     (const SDL_Vertex *)vertices, vertex_count, NULL, 0);
}

static HOST_STORE_CHUNK_SIG(do_store_chunk) {
  bool ok =
      storage_file_set(&save_file, chunk_key, data, data_size) == STORAGE_OK;
  game_api.game_chunk_stored(chunk_key, ok);
}

static HOST_LOAD_CHUNK_SIG(do_load_chunk) {
  uint8_t buffer[1024 * 1024];
  uint32_t size = sizeof(buffer);
  if (storage_file_get(&save_file, chunk_key, buffer, &size) != STORAGE_OK) {
    size = 0;
  }
  game_api.game_chunk_loaded(chunk_key, buffer, size);
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
  api->game_set_host_functions = (GameSetHostFunctionsFn)SDL_LoadFunction(
      api->lib_handle, TOSTRING(GAME_SET_HOST_FUNCTIONS_NAME));
  api->game_get_memory_size = (GameGetMemorySizeFn)SDL_LoadFunction(
      api->lib_handle, TOSTRING(GAME_GET_MEMORY_SIZE_NAME));
  api->game_set_memory = (GameSetMemoryFn)SDL_LoadFunction(
      api->lib_handle, TOSTRING(GAME_SET_MEMORY_NAME));
  api->game_init =
      (GameInitFn)SDL_LoadFunction(api->lib_handle, TOSTRING(GAME_INIT_NAME));
  api->game_input =
      (GameInputFn)SDL_LoadFunction(api->lib_handle, TOSTRING(GAME_INPUT_NAME));
  api->game_frame =
      (GameFrameFn)SDL_LoadFunction(api->lib_handle, TOSTRING(GAME_FRAME_NAME));
  api->game_render = (GameRenderFn)SDL_LoadFunction(api->lib_handle,
                                                    TOSTRING(GAME_RENDER_NAME));
  api->game_chunk_stored = (GameChunkStoredFn)SDL_LoadFunction(
      api->lib_handle, TOSTRING(GAME_CHUNK_STORED_NAME));
  api->game_chunk_loaded = (GameChunkLoadedFn)SDL_LoadFunction(
      api->lib_handle, TOSTRING(GAME_CHUNK_LOADED_NAME));

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

  bool needs_init = false;
  size_t memory_size = api->game_get_memory_size();
  if (memory_size != state_memory_size) {
    state_memory = realloc(state_memory, memory_size);
    state_memory_size = memory_size;
    needs_init = true;
  }

  game_api.game_set_host_functions(do_log, do_submit_geometry, do_load_chunk,
                                   do_store_chunk);
  game_api.game_set_memory(state_memory, state_memory_size);

  if (needs_init) {
    game_api.game_init(random_seed);
  }

  return true;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  random_seed = SDL_rand_bits();

  storage_file_run_tests();

  if (storage_file_open(&save_file, "savegame.dat") != STORAGE_OK) {
    return 1;
  }

  // Load game library
  const char *lib_path = "./libgame.so";
  if (!load_game_api(&game_api, lib_path)) {
    return 1;
  }

  renderer = (Renderer){
      .scale = 2, // Default 2x scaling
  };
  if (!init_renderer(&renderer)) {
    return 1;
  }

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

        InputCommand cmd = map_key_to_command(event.key.key);
        if (cmd != INPUT_CMD_NONE) {
          game_api.game_input(cmd);
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
    game_api.game_frame(dt);

    // Render
    // Clear to black
    SDL_SetRenderDrawColor(renderer.renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer.renderer);

    // Call game render
    game_api.game_render(renderer.window_width, renderer.window_height,
                         renderer.scaled_tile_size, renderer.atlas_width,
                         renderer.atlas_height);

    // Present
    SDL_RenderPresent(renderer.renderer);
  }

  storage_file_close(&save_file);

  // Cleanup
  if (game_api.lib_handle) {
    SDL_UnloadObject(game_api.lib_handle);
  }
  shutdown_renderer(&renderer);
  return 0;
}
