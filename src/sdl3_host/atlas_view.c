#include "atlas_view.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#define TILE_SIZE 12
#define TILE_PADDING 1
#define FAST_MOVE_STEP 4 // Number of tiles to move when shift is held

void atlas_viewer_run(SDL_Window *window, SDL_Renderer *renderer,
                      SDL_Texture *atlas_texture, int atlas_width,
                      int atlas_height, int atlas_cols, int atlas_rows, int scale) {
  (void)atlas_width;
  (void)atlas_height;

  // Get actual pixel dimensions to calculate viewport
  int window_width, window_height;
  SDL_GetWindowSizeInPixels(window, &window_width, &window_height);

  int scaled_tile_size = TILE_SIZE * scale;
  int viewport_tiles_x = window_width / scaled_tile_size;
  int viewport_tiles_y = window_height / scaled_tile_size;

  int selected_x = 0;
  int selected_y = 0;
  int camera_x = 0; // Top-left corner of viewport in tile coordinates
  int camera_y = 0;
  bool running = true;

  printf("\n=== Atlas Viewer Mode ===\n");
  printf("Use arrow keys to navigate, Q/ESC to exit\n");
  printf("Selected tile: %d (x=%d, y=%d)\n", selected_y * atlas_cols + selected_x,
         selected_x, selected_y);

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running = false;
        break;

      case SDL_EVENT_KEY_DOWN:
        switch (event.key.key) {
        case SDLK_ESCAPE:
        case SDLK_Q:
          running = false;
          break;

        case SDLK_UP: {
          bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          int step = shift_held ? FAST_MOVE_STEP : 1;
          if (selected_y > 0) {
            selected_y -= step;
            if (selected_y < 0) selected_y = 0;
            // Scroll camera up if selection goes off top
            if (selected_y < camera_y) {
              camera_y = selected_y;
            }
            printf("Selected tile: %d (x=%d, y=%d)\n",
                   selected_y * atlas_cols + selected_x, selected_x, selected_y);
          }
          break;
        }

        case SDLK_DOWN: {
          bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          int step = shift_held ? FAST_MOVE_STEP : 1;
          if (selected_y < atlas_rows - 1) {
            selected_y += step;
            if (selected_y >= atlas_rows) selected_y = atlas_rows - 1;
            // Scroll camera down if selection goes off bottom
            if (selected_y >= camera_y + viewport_tiles_y) {
              camera_y = selected_y - viewport_tiles_y + 1;
            }
            printf("Selected tile: %d (x=%d, y=%d)\n",
                   selected_y * atlas_cols + selected_x, selected_x, selected_y);
          }
          break;
        }

        case SDLK_LEFT: {
          bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          int step = shift_held ? FAST_MOVE_STEP : 1;
          if (selected_x > 0) {
            selected_x -= step;
            if (selected_x < 0) selected_x = 0;
            // Scroll camera left if selection goes off left edge
            if (selected_x < camera_x) {
              camera_x = selected_x;
            }
            printf("Selected tile: %d (x=%d, y=%d)\n",
                   selected_y * atlas_cols + selected_x, selected_x, selected_y);
          }
          break;
        }

        case SDLK_RIGHT: {
          bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          int step = shift_held ? FAST_MOVE_STEP : 1;
          if (selected_x < atlas_cols - 1) {
            selected_x += step;
            if (selected_x >= atlas_cols) selected_x = atlas_cols - 1;
            // Scroll camera right if selection goes off right edge
            if (selected_x >= camera_x + viewport_tiles_x) {
              camera_x = selected_x - viewport_tiles_x + 1;
            }
            printf("Selected tile: %d (x=%d, y=%d)\n",
                   selected_y * atlas_cols + selected_x, selected_x, selected_y);
          }
          break;
        }

        default:
          break;
        }
        break;

      default:
        break;
      }
    }

    // Render
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Draw visible tiles from atlas
    for (int ty = camera_y; ty < camera_y + viewport_tiles_y && ty < atlas_rows; ty++) {
      for (int tx = camera_x; tx < camera_x + viewport_tiles_x && tx < atlas_cols; tx++) {
        // Calculate tile position in atlas (accounting for padding)
        int atlas_x = TILE_PADDING + tx * (TILE_SIZE + TILE_PADDING);
        int atlas_y = TILE_PADDING + ty * (TILE_SIZE + TILE_PADDING);

        SDL_FRect src = {
            .x = (float)atlas_x,
            .y = (float)atlas_y,
            .w = TILE_SIZE,
            .h = TILE_SIZE,
        };

        // Screen position relative to camera
        int screen_x = (tx - camera_x) * scaled_tile_size;
        int screen_y = (ty - camera_y) * scaled_tile_size;

        SDL_FRect dst = {
            .x = (float)screen_x,
            .y = (float)screen_y,
            .w = (float)scaled_tile_size,
            .h = (float)scaled_tile_size,
        };

        SDL_RenderTexture(renderer, atlas_texture, &src, &dst);
      }
    }

    // Draw highlight rectangle around selected tile (in screen space)
    int highlight_screen_x = (selected_x - camera_x) * scaled_tile_size;
    int highlight_screen_y = (selected_y - camera_y) * scaled_tile_size;

    SDL_FRect highlight = {
        .x = (float)highlight_screen_x,
        .y = (float)highlight_screen_y,
        .w = (float)scaled_tile_size,
        .h = (float)scaled_tile_size,
    };

    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow
    SDL_RenderRect(renderer, &highlight);

    // Draw a thicker highlight (2 pixel border)
    highlight.x -= 1;
    highlight.y -= 1;
    highlight.w += 2;
    highlight.h += 2;
    SDL_RenderRect(renderer, &highlight);

    SDL_RenderPresent(renderer);

    // Small delay to avoid busy-waiting
    SDL_Delay(16); // ~60 FPS
  }

  printf("=== Exiting Atlas Viewer ===\n\n");
}
