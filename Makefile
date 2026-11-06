# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
CORE_SRC := src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_tiles.c
HOST_SRC := src/lua/wb_lua_host.c
OBJ     := build/lua.o build/wb_ecs.o build/wb_host.o
TARGET  := build/wubu_engine.exe

.PHONY: all clean run test

all: $(TARGET)

build:
	mkdir -p build

$(TARGET): build $(CORE_SRC) $(HOST_SRC) $(LUA_SRC)
	$(CC) $(CFLAGS) $(CORE_SRC) $(HOST_SRC) $(LUA_SRC) -o $@ -lm

run: $(TARGET)
	./$(TARGET) games/main.lua

# test: build each core module standalone (no Lua host/main) + run its harness
test: test-ecs test-spatial test-tiles

TEST_ECS := build/wb_test.exe
test-ecs: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_ecs.c tests/wb_test.c -o $(TEST_ECS)
	./$(TEST_ECS)

TEST_SPATIAL := build/wb_spatial_test.exe
test-spatial: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c tests/wb_spatial_test.c -o $(TEST_SPATIAL) -lm
	./$(TEST_SPATIAL)

TEST_TILES := build/wb_tiles_test.exe
test-tiles: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c src/core/wb_tiles.c tests/wb_tiles_test.c -o $(TEST_TILES) -lm
	./$(TEST_TILES)

clean:
	rm -rf build
