# DGEngine Build System
# Usage:
#   make          — release build
#   make debug    — debug build with sanitizers
#   make test     — build + run unit tests (no display required)
#   make clean    — remove build artifacts

CC      = gcc
STD     = -std=c11
WARN    = -Wall -Wextra -Wpedantic

INCLUDES = -Iexternal/glad/include -Iexternal/stb -Isrc -I/usr/include/lua5.4

LIBS     = -lSDL2 -lGL -lm -llua5.4

SRC_ALL     = $(shell find src external -name "*.c")

# The editor gets everything except runtime_main.c
SRC_EDITOR  = $(filter-out src/runtime_main.c, $(SRC_ALL))

# The runtime gets core engine + shared UI (fonts/text/theme) + runtime_main.c
SRC_CORE    = $(filter-out src/main.c src/runtime_main.c src/editor/% src/ui/%, $(SRC_ALL))
SRC_UI_COMM = src/ui/font.c src/ui/font_atlas.c src/ui/text.c src/ui/theme.c src/ui/left_pane.c src/ui/inspector_pane.c
SRC_RUNTIME = $(SRC_CORE) $(SRC_UI_COMM) src/runtime_main.c

OUT_EDITOR  = bin/dgengine
OUT_RUNTIME = bin/dgruntime

CFLAGS_RELEASE = $(STD) $(WARN) -O2
CFLAGS_DEBUG   = $(STD) $(WARN) -g -O0 -fsanitize=address,undefined

all: $(OUT_EDITOR) $(OUT_RUNTIME)

$(OUT_EDITOR): $(SRC_EDITOR)
	@mkdir -p bin
	$(CC) $(SRC_EDITOR) $(CFLAGS_RELEASE) $(INCLUDES) $(LIBS) -o $(OUT_EDITOR)
	@echo "Build Editor OK -> $(OUT_EDITOR)"

$(OUT_RUNTIME): $(SRC_RUNTIME)
	@mkdir -p bin
	$(CC) $(SRC_RUNTIME) $(CFLAGS_RELEASE) $(INCLUDES) $(LIBS) -o $(OUT_RUNTIME)
	@echo "Build Runtime OK -> $(OUT_RUNTIME)"

debug:
	@mkdir -p bin
	$(CC) $(SRC_EDITOR) $(CFLAGS_DEBUG) $(INCLUDES) $(LIBS) -o $(OUT_EDITOR)
	$(CC) $(SRC_RUNTIME) $(CFLAGS_DEBUG) $(INCLUDES) $(LIBS) -o $(OUT_RUNTIME)
	@echo "Debug build OK -> $(OUT_EDITOR) & $(OUT_RUNTIME)"

# Tests link only the .c files they actually need (no SDL2/GL), so they
# build and run anywhere — useful in CI or anywhere without a display.
TEST_DIR = bin/tests

test: $(TEST_DIR)/test_registry $(TEST_DIR)/test_picking $(TEST_DIR)/test_pathfinder $(TEST_DIR)/test_weather $(TEST_DIR)/test_simulation $(TEST_DIR)/test_construction $(TEST_DIR)/test_spatial_grid $(TEST_DIR)/test_world_save $(TEST_DIR)/test_world_generator $(TEST_DIR)/test_level
	@echo "--- running tests ---"
	@./$(TEST_DIR)/test_registry
	@./$(TEST_DIR)/test_picking
	@./$(TEST_DIR)/test_pathfinder
	@./$(TEST_DIR)/test_weather
	@./$(TEST_DIR)/test_simulation
	@./$(TEST_DIR)/test_construction
	@./$(TEST_DIR)/test_spatial_grid
	@./$(TEST_DIR)/test_world_save
	@./$(TEST_DIR)/test_world_generator
	@./$(TEST_DIR)/test_level

$(TEST_DIR)/test_registry: tests/test_registry.c src/ecs/registry.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_picking: tests/test_picking.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_pathfinder: tests/test_pathfinder.c src/ai/pathfinder.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_weather: tests/test_weather.c src/simulation/weather.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_simulation: tests/test_simulation.c src/simulation/simulation.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_construction: tests/test_construction.c src/simulation/construction.c src/simulation/simulation.c src/ecs/registry.c src/core/log.c src/core/object_def.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_spatial_grid: tests/test_spatial_grid.c src/world/spatial_grid.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_world_save: tests/test_world_save.c src/world/world.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_world_generator: tests/test_world_generator.c src/world/world.c src/world/world_generator.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

$(TEST_DIR)/test_level: tests/test_level.c src/core/level.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

clean:
	rm -f $(OUT_EDITOR) $(OUT_RUNTIME)
	rm -rf $(TEST_DIR)

.PHONY: all debug test clean
