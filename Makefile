# DGEngine Build System
# Usage:
#   make          — release build
#   make debug    — debug build with sanitizers
#   make test     — build + run unit tests (no display required)
#   make clean    — remove build artifacts

CC      = gcc
STD     = -std=c11
WARN    = -Wall -Wextra -Wpedantic

INCLUDES = -Iexternal/glad/include -Iexternal/stb -Isrc

LIBS     = -lSDL2 -lGL -lm

SRC     = $(shell find src external -name "*.c")
OUT     = bin/dgengine

CFLAGS_RELEASE = $(STD) $(WARN) -O2
CFLAGS_DEBUG   = $(STD) $(WARN) -g -O0 -fsanitize=address,undefined

all: $(OUT)

$(OUT): $(SRC)
	@mkdir -p bin
	$(CC) $(SRC) $(CFLAGS_RELEASE) $(INCLUDES) $(LIBS) -o $(OUT)
	@echo "Build OK -> $(OUT)"

debug:
	@mkdir -p bin
	$(CC) $(SRC) $(CFLAGS_DEBUG) $(INCLUDES) $(LIBS) -o $(OUT)
	@echo "Debug build OK -> $(OUT)"

# Tests link only the .c files they actually need (no SDL2/GL), so they
# build and run anywhere — useful in CI or anywhere without a display.
TEST_DIR = bin/tests

test: $(TEST_DIR)/test_registry $(TEST_DIR)/test_picking $(TEST_DIR)/test_pathfinder $(TEST_DIR)/test_weather $(TEST_DIR)/test_simulation $(TEST_DIR)/test_construction
	@echo "--- running tests ---"
	@./$(TEST_DIR)/test_registry
	@./$(TEST_DIR)/test_picking
	@./$(TEST_DIR)/test_pathfinder
	@./$(TEST_DIR)/test_weather
	@./$(TEST_DIR)/test_simulation
	@./$(TEST_DIR)/test_construction

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

$(TEST_DIR)/test_construction: tests/test_construction.c src/simulation/construction.c src/simulation/simulation.c src/ecs/registry.c src/core/log.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS_DEBUG) $(INCLUDES) $^ -o $@ -lm

clean:
	rm -f $(OUT)
	rm -rf $(TEST_DIR)

.PHONY: all debug test clean
