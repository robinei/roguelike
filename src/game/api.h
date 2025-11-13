#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Helper macros for stringification
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Log levels mostly having equivalents in JavaScript and SDL3
typedef enum {
  LOG_DEBUG,
  LOG_LOG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
} LogLevel;

// Vertex format compatible with SDL_Vertex
// This exact layout matches SDL_Vertex so hosts can cast directly
typedef struct {
  float position[2];  // Screen position in pixels (x, y)
  float color[4];     // Vertex color (r, g, b, a) in 0-1 range
  float tex_coord[2]; // Texture coordinates (u, v) in 0-1 range
} Vertex;

typedef enum {
  INPUT_CMD_NONE,

  INPUT_CMD_UP,
  INPUT_CMD_RIGHT,
  INPUT_CMD_DOWN,
  INPUT_CMD_LEFT,

  INPUT_CMD_PERIOD,
  INPUT_CMD_R,
  INPUT_CMD_D,
} InputCommand;

// ============================================================================
// Functions exposed by the Host for use by the Game:
// ============================================================================

#define HOST_LOG_NAME host_log
#define HOST_LOG_SIG(name) void name(LogLevel level, const char *message)
typedef HOST_LOG_SIG((*HostLogFn));

#define HOST_SUBMIT_GEOMETRY_NAME host_submit_geometry
#define HOST_SUBMIT_GEOMETRY_SIG(name)                                         \
  void name(const Vertex *vertices, int vertex_count)
typedef HOST_SUBMIT_GEOMETRY_SIG((*HostSubmitGeometryFn));

#define HOST_LOAD_CHUNK_NAME host_load_chunk
#define HOST_LOAD_CHUNK_SIG(name) void name(uint64_t chunk_key)
typedef HOST_LOAD_CHUNK_SIG((*HostLoadChunkFn));

#define HOST_STORE_CHUNK_NAME host_store_chunk
#define HOST_STORE_CHUNK_SIG(name)                                             \
  void name(uint64_t chunk_key, const void *data, size_t data_size)
typedef HOST_STORE_CHUNK_SIG((*HostStoreChunkFn));

#ifdef __wasm__
// for WASM the interface to the Host will be imported as externs from JS:
extern HOST_LOG_SIG(HOST_LOG_NAME);
extern HOST_SUBMIT_GEOMETRY_SIG(HOST_SUBMIT_GEOMETRY_NAME);
extern HOST_LOAD_CHUNK_SIG(HOST_LOAD_CHUNK_NAME);
extern HOST_STORE_CHUNK_SIG(HOST_STORE_CHUNK_NAME);
#else
// when compiled for native, the interface to the Host will be provided by the
// host (in a call to game_set_host_functions):
extern HostLogFn HOST_LOG_NAME;
extern HostSubmitGeometryFn HOST_SUBMIT_GEOMETRY_NAME;
extern HostLoadChunkFn HOST_LOAD_CHUNK_NAME;
extern HostStoreChunkFn HOST_STORE_CHUNK_NAME;
#endif

// ============================================================================
// Functions exposed by the Game for use by the Host:
// ============================================================================

#ifndef __wasm__
#define GAME_SET_HOST_FUNCTIONS_NAME game_set_host_functions
#define GAME_SET_HOST_FUNCTIONS_SIG(name)                                      \
  void name(HostLogFn host_log_fn,                                             \
            HostSubmitGeometryFn host_submit_geometry_fn,                      \
            HostLoadChunkFn host_load_chunk_fn,                                \
            HostStoreChunkFn host_store_chunk_fn)
typedef GAME_SET_HOST_FUNCTIONS_SIG((*GameSetHostFunctionsFn));
#endif

#define GAME_GET_MEMORY_SIZE_NAME game_get_memory_size
#define GAME_GET_MEMORY_SIZE_SIG(name) size_t name(void)
typedef GAME_GET_MEMORY_SIZE_SIG((*GameGetMemorySizeFn));

#define GAME_SET_MEMORY_NAME game_set_memory
#define GAME_SET_MEMORY_SIG(name) void name(void *buf, size_t size)
typedef GAME_SET_MEMORY_SIG((*GameSetMemoryFn));

#define GAME_INIT_NAME game_init
#define GAME_INIT_SIG(name) void name(uint64_t rng_seed)
typedef GAME_INIT_SIG((*GameInitFn));

#define GAME_INPUT_NAME game_input
#define GAME_INPUT_SIG(name) void name(InputCommand command)
typedef GAME_INPUT_SIG((*GameInputFn));

#define GAME_FRAME_NAME game_frame
#define GAME_FRAME_SIG(name) void name(double dt)
typedef GAME_FRAME_SIG((*GameFrameFn));

#define GAME_RENDER_NAME game_render
#define GAME_RENDER_SIG(name)                                                  \
  void name(int viewport_width_px, int viewport_height_px, int tile_size,      \
            int atlas_width_px, int atlas_height_px)
typedef GAME_RENDER_SIG((*GameRenderFn));

#define GAME_CHUNK_STORED_NAME game_chunk_stored
#define GAME_CHUNK_STORED_SIG(name) void name(uint64_t chunk_key, bool ok)
typedef GAME_CHUNK_STORED_SIG((*GameChunkStoredFn));

#define GAME_CHUNK_LOADED_NAME game_chunk_loaded
#define GAME_CHUNK_LOADED_SIG(name)                                            \
  void name(uint64_t chunk_key, const void *data, size_t data_size)
typedef GAME_CHUNK_LOADED_SIG((*GameChunkLoadedFn));
