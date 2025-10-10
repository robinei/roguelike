CC = clang
CFLAGS = -std=c99 -Wall -Wextra -g -MMD -MP
BUILDDIR = build
WASM_CC = clang
WASM_CFLAGS = -std=c99 -Wall -Wextra -O3 --target=wasm32 -D__wasm__
WASM_LDFLAGS = -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -Wl,--import-memory

# Game shared library
GAME_SRCS = $(shell find src/game -name '*.c')
GAME_OBJS = $(GAME_SRCS:%.c=$(BUILDDIR)/%.o)
GAME_DEPS = $(GAME_SRCS:%.c=$(BUILDDIR)/%.d)
GAME_LIB = $(BUILDDIR)/libgame.so
GAME_CFLAGS = $(CFLAGS) -fPIC
GAME_LDFLAGS = -shared -lm

# SDL3 host executable
HOST_SRCS = $(shell find src/sdl3_host -name '*.c')
HOST_OBJS = $(HOST_SRCS:%.c=$(BUILDDIR)/%.o)
HOST_DEPS = $(HOST_SRCS:%.c=$(BUILDDIR)/%.d)
HOST_TARGET = $(BUILDDIR)/roguelike
HOST_LDFLAGS = -lm -lSDL3

# WASM build
WASM_OBJS = $(GAME_SRCS:%.c=$(BUILDDIR)/wasm/%.o)
WASM_TARGET = $(BUILDDIR)/game.wasm

all: $(GAME_LIB) $(HOST_TARGET)

wasm: $(WASM_TARGET)

# Build game shared library (atomic move for hot-reload safety)
$(GAME_LIB): $(GAME_OBJS) | $(BUILDDIR)
	$(CC) $(GAME_LDFLAGS) $(GAME_OBJS) -o $(GAME_LIB).tmp
	mv $(GAME_LIB).tmp $(GAME_LIB)

# Build host executable
$(HOST_TARGET): $(HOST_OBJS) | $(BUILDDIR)
	$(CC) $(HOST_LDFLAGS) $(HOST_OBJS) -o $(HOST_TARGET)

# Compile game source files with -fPIC
$(BUILDDIR)/src/game/%.o: src/game/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) -c $< -o $@

# Compile host source files
$(BUILDDIR)/src/sdl3_host/%.o: src/sdl3_host/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Build WASM module
$(WASM_TARGET): $(WASM_OBJS) | $(BUILDDIR)
	$(WASM_CC) --target=wasm32 $(WASM_LDFLAGS) $(WASM_OBJS) -o $(WASM_TARGET)

# Compile game source files for WASM
$(BUILDDIR)/wasm/src/game/%.o: src/game/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(GAME_DEPS)
-include $(HOST_DEPS)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean wasm