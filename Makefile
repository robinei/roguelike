# ============================================================================
# Compiler and build configuration
# ============================================================================
CC = clang
CFLAGS = -std=c99 -Wall -Wextra -g -MMD -MP
BUILDDIR = build
GAME_SRCS = $(shell find src/game -name '*.c')
GAME_DEPS = $(GAME_SRCS:%.c=$(BUILDDIR)/%.d)

# ============================================================================
# Game shared library (for hot-reload in native host)
# ============================================================================
GAMELIB_CFLAGS = $(CFLAGS) -fPIC
GAMELIB_LDFLAGS = -shared -lm
GAMELIB_OBJS = $(GAME_SRCS:%.c=$(BUILDDIR)/%.o)
GAMELIB_TARGET = $(BUILDDIR)/libgame.so

# ============================================================================
# WASM build (for web host)
# ============================================================================
WASM_CC = clang
WASM_CFLAGS = -std=c99 -Wall -Wextra -O3 --target=wasm32 -D__wasm__
WASM_LDFLAGS = -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -Wl,--import-memory
WASM_OBJS = $(GAME_SRCS:%.c=$(BUILDDIR)/wasm/%.o)
WASM_TARGET = $(BUILDDIR)/game.wasm

# ============================================================================
# SDL3 host executable (native desktop)
# ============================================================================
HOSTBIN_LDFLAGS = -lm -lSDL3
HOSTBIN_SRCS = $(shell find src/sdl3_host -name '*.c')
HOSTBIN_OBJS = $(HOSTBIN_SRCS:%.c=$(BUILDDIR)/%.o)
HOSTBIN_DEPS = $(HOSTBIN_SRCS:%.c=$(BUILDDIR)/%.d)
HOSTBIN_TARGET = $(BUILDDIR)/roguelike

# ============================================================================
# Web host
# ============================================================================
WEB_HOST_DIR = $(BUILDDIR)/web
WEB_HOST_ASSETS = urizen_onebit_tileset__v2d0.png cp437_12x12.png
WEB_HOST_SOURCES = src/web_host/index.html src/web_host/game.js

# ============================================================================
# Phony targets
# ============================================================================
.PHONY: all clean wasm serve

all: $(GAMELIB_TARGET) $(HOSTBIN_TARGET)

wasm: $(WASM_TARGET)
	@mkdir -p $(WEB_HOST_DIR)
	@cp $(WEB_HOST_SOURCES) $(WEB_HOST_DIR)/
	@cp $(WEB_HOST_ASSETS) $(WEB_HOST_DIR)/
	@cp $(WASM_TARGET) $(WEB_HOST_DIR)/
	@echo "Web build complete. Serve $(WEB_HOST_DIR)/ with a web server."

serve: wasm
	@cd "$(WEB_HOST_DIR)"; python3 -m http.server 8000

clean:
	rm -rf $(BUILDDIR)

# ============================================================================
# Native build rules
# ============================================================================

# Game shared library (atomic move for hot-reload safety)
$(GAMELIB_TARGET): $(GAMELIB_OBJS) | $(BUILDDIR)
	$(CC) $(GAMELIB_LDFLAGS) $(GAMELIB_OBJS) -o $(GAMELIB_TARGET).tmp
	@mv $(GAMELIB_TARGET).tmp $(GAMELIB_TARGET)

# Host executable
$(HOSTBIN_TARGET): $(HOSTBIN_OBJS) | $(BUILDDIR)
	$(CC) $(HOSTBIN_LDFLAGS) $(HOSTBIN_OBJS) -o $(HOSTBIN_TARGET)

# Compile game source files with -fPIC
$(BUILDDIR)/src/game/%.o: src/game/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(GAMELIB_CFLAGS) -c $< -o $@

# Compile host source files
$(BUILDDIR)/src/sdl3_host/%.o: src/sdl3_host/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# WASM build rules
# ============================================================================

# Link WASM module
$(WASM_TARGET): $(WASM_OBJS) | $(BUILDDIR)
	$(WASM_CC) --target=wasm32 $(WASM_LDFLAGS) $(WASM_OBJS) -o $(WASM_TARGET)

# Compile game source files for WASM
$(BUILDDIR)/wasm/src/game/%.o: src/game/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

# ============================================================================
# Build directory and dependencies
# ============================================================================

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

-include $(GAME_DEPS)
-include $(HOSTBIN_DEPS)
