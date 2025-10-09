#include "atlas_view.h"
#include "game.h"
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

// Tile enum - semantic names for tiles
typedef enum {
  TILE_FLOOR = 0,
  TILE_WALL = 1,
  TILE_PLAYER = 2884,
  TILE_DOOR = 50,
  // Add more as needed based on the tileset
} TileType;

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

// Draw a single character using CP437 font
static void draw_char(Renderer *r, unsigned char ch, int screen_x,
                      int screen_y) {
  int scaled_size = r->scaled_tile_size;

  // Draw semi-transparent black background behind the character
  SDL_FRect bg = {
      .x = (float)screen_x,
      .y = (float)screen_y,
      .w = (float)scaled_size,
      .h = (float)scaled_size,
  };
  SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 192); // 75% opacity
  SDL_RenderFillRect(r->renderer, &bg);

  // CP437 layout: 16 columns x 16 rows
  int glyph_x = (ch % 16) * TILE_SIZE;
  int glyph_y = (ch / 16) * TILE_SIZE;

  SDL_FRect src = {
      .x = (float)glyph_x,
      .y = (float)glyph_y,
      .w = TILE_SIZE,
      .h = TILE_SIZE,
  };

  SDL_FRect dst = {
      .x = (float)screen_x,
      .y = (float)screen_y,
      .w = (float)scaled_size,
      .h = (float)scaled_size,
  };

  SDL_RenderTexture(r->renderer, r->font_texture, &src, &dst);
}

// Draw a string of text
static void draw_text(Renderer *r, const char *text, int screen_x,
                      int screen_y) {
  int x = screen_x;
  int scaled_size = r->scaled_tile_size;
  for (const char *p = text; *p; p++) {
    draw_char(r, (unsigned char)*p, x, screen_y);
    x += scaled_size;
  }
}

// Draw a single tile at screen position (x, y) in pixels
static void draw_tile(Renderer *r, int tile_index, int screen_x, int screen_y) {
  // Calculate tile position in atlas, accounting for padding
  int tile_x = tile_index % r->atlas_cols;
  int tile_y = tile_index / r->atlas_cols;

  // Atlas position with padding
  int atlas_x = TILE_PADDING + tile_x * (TILE_SIZE + TILE_PADDING);
  int atlas_y = TILE_PADDING + tile_y * (TILE_SIZE + TILE_PADDING);

  int scaled_size = r->scaled_tile_size;

  SDL_FRect src = {
      .x = (float)atlas_x,
      .y = (float)atlas_y,
      .w = TILE_SIZE,
      .h = TILE_SIZE,
  };

  SDL_FRect dst = {
      .x = (float)screen_x,
      .y = (float)screen_y,
      .w = (float)scaled_size,
      .h = (float)scaled_size,
  };

  SDL_RenderTexture(r->renderer, r->atlas_texture, &src, &dst);
}

static void render_world(Renderer *r, WorldState *world) {
  // Clear to black
  SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
  SDL_RenderClear(r->renderer);

  // Get player position for camera centering
  EntityIndex player_idx = entity_handle_to_index(world->player);
  int camera_center_x = 0;
  int camera_center_y = 0;

  if (entity_has(player_idx, position)) {
    camera_center_x = world->position[player_idx].x;
    camera_center_y = world->position[player_idx].y;
  }

  // Calculate top-left corner of viewport in world coordinates
  int viewport_left = camera_center_x - r->viewport_tiles_x / 2;
  int viewport_top = camera_center_y - r->viewport_tiles_y / 2;

  // Draw visible tiles
  for (int screen_y = 0; screen_y < r->viewport_tiles_y; screen_y++) {
    for (int screen_x = 0; screen_x < r->viewport_tiles_x; screen_x++) {
      int world_x = viewport_left + screen_x;
      int world_y = viewport_top + screen_y;

      // Check if tile is within map bounds
      if (world_x < 0 || world_x >= (int)world->map.width || world_y < 0 ||
          world_y >= (int)world->map.height) {
        // Out of bounds - draw nothing (already cleared to black)
        continue;
      }

      int tile = TILE_FLOOR;

      // Draw checkerboard pattern as test
      if ((world_x + world_y) % 2 == 0) {
        tile = 0; // First tile
      } else {
        tile = 1; // Second tile
      }

      draw_tile(r, tile, screen_x * r->scaled_tile_size,
                screen_y * r->scaled_tile_size);
    }
  }

  // Draw player at center of screen
  if (entity_has(player_idx, position)) {
    int player_screen_x = (r->viewport_tiles_x / 2) * r->scaled_tile_size;
    int player_screen_y = (r->viewport_tiles_y / 2) * r->scaled_tile_size;
    draw_tile(r, TILE_PLAYER, player_screen_x, player_screen_y);
  }

  // Draw message log at bottom of screen
  // Calculate how many viewport tiles the messages occupy
  int message_viewport_tiles = MESSAGE_DISPLAY_LINES;

  // Check if the bottom-most tile is partial (would cut off last message line)
  bool bottom_tile_partial = (r->window_height % r->scaled_tile_size) != 0;

  // If bottom tile is partial, start messages one tile higher
  int viewport_start_y = r->viewport_tiles_y - message_viewport_tiles;
  if (bottom_tile_partial && viewport_start_y > 0) {
    viewport_start_y--;
  }
  if (viewport_start_y < 0)
    viewport_start_y = 0;

  int message_area_y = viewport_start_y * r->scaled_tile_size;

  // Draw messages from circular buffer (draw_char will draw backgrounds
  // per-glyph)
  int messages_to_show = MESSAGE_DISPLAY_LINES;
  if (messages_to_show > (int)world->messages_count) {
    messages_to_show = world->messages_count;
  }

  // Calculate which messages to show based on scroll offset
  int start_msg_idx =
      (int)world->messages_count - messages_to_show - r->message_scroll_offset;
  if (start_msg_idx < 0)
    start_msg_idx = 0;

  for (int i = 0;
       i < messages_to_show && start_msg_idx + i < (int)world->messages_count;
       i++) {
    int msg_idx =
        (world->messages_first + start_msg_idx + i) % MESSAGE_COUNT_MAX;
    int y = message_area_y + i * r->scaled_tile_size;
    draw_text(r, world->messages[msg_idx].text, 0, y);
  }

  SDL_RenderPresent(r->renderer);
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
        // double fps = frame_count / elapsed_sec;
        // printf("FPS: %.1f\n", fps);
        (void)elapsed_sec; // Suppress unused warning
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
    render_world(&renderer, &world);
    frame_count++;
  }

  shutdown_renderer(&renderer);
  return 0;
}
