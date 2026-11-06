# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
CORE_SRC := src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_tiles.c src/core/wb_render.c src/core/wb_exp.c
SCRIPT_SRC := src/lua/wb_script.c
HOST_SRC := src/lua/wb_lua_host.c
OBJ     := build/lua.o build/wb_ecs.o build/wb_host.o
TARGET  := build/wubu_engine.exe

.PHONY: all clean run test demo

all: $(TARGET)

build:
	mkdir -p build

$(TARGET): build $(CORE_SRC) $(SCRIPT_SRC) $(HOST_SRC) $(LUA_SRC)
	$(CC) $(CFLAGS) -Isrc/lua $(CORE_SRC) $(SCRIPT_SRC) $(HOST_SRC) $(LUA_SRC) -o $@ -lm

run: $(TARGET)
	./$(TARGET) games/main.lua

# test: build each core module standalone (no Lua host/main) + run its harness
test: test-ecs test-spatial test-tiles test-render test-exp test-lv

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

TEST_RENDER := build/wb_render_test.exe
test-render: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_render.c tests/wb_render_test.c -o $(TEST_RENDER) -lm
	./$(TEST_RENDER)

TEST_EXP := build/wb_exp_test.exe
test-exp: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_exp.c src/lua/wb_script.c tests/wb_exp_test.c $(LUA_SRC) -o $(TEST_EXP) -lm
	./$(TEST_EXP)

# test-lv: the slerm-Lua interpreter (two harnesses)
TEST_LV  := build/wb_lv_test.exe
TEST_LVS := build/wb_lv_script_test.exe
test-lv: test-lv-core test-lv-script
test-lv-core: build
	$(CC) $(CFLAGS) -Isrc/lv src/lv/wb_lv.c tests/wb_lv_test.c -o $(TEST_LV) -lm
	./$(TEST_LV)
test-lv-script: build
	$(CC) $(CFLAGS) -Isrc/lv src/lv/wb_lv.c tests/wb_lv_script_test.c -o $(TEST_LVS) -lm
	./$(TEST_LVS)

clean:
	rm -rf build

# geo demo: the flying-camera / ambient-culling loop, C11-only
DEMO_OBJ := build/wb_geo_demo.exe
demo: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c src/core/wb_tiles.c examples/wb_geo_demo.c -o $(DEMO_OBJ) -lm
	./$(DEMO_OBJ)
