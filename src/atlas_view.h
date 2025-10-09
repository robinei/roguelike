#pragma once

#include <SDL3/SDL.h>

// Enter atlas viewer mode - takes over rendering until user exits with Q/ESC
// Returns when user exits the viewer
void atlas_viewer_run(SDL_Window *window, SDL_Renderer *renderer,
                      SDL_Texture *atlas_texture, int atlas_width,
                      int atlas_height, int atlas_cols, int atlas_rows, int scale);
