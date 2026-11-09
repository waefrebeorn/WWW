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
test: test-ecs test-spatial test-tiles test-render test-exp test-lv test-physics test-character test-gpu test-assets test-net test-avatar test-mod test-dist test-edit test-mesh

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

# test-physics: slice 9 -- MuJoCo-backed physics world
MUJOCO_DIR  := thirdparty/mujoco
TEST_PHYSICS := build/wb_physics_test.exe
test-physics: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c src/core/wb_physics.c tests/wb_physics_test.c -o $(TEST_PHYSICS) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(TEST_PHYSICS)

# test-character: slice 10 -- input + camera + character controller chain
TEST_CHAR := build/wb_character_test.exe
test-character: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c src/core/wb_physics.c src/core/wb_input.c src/core/wb_camera.c src/core/wb_character.c tests/wb_character_test.c -o $(TEST_CHAR) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(TEST_CHAR)

# demo-character: slice 10 headless playable-character demo
DEMO_CHAR := build/wb_character_demo.exe
demo-character: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c src/core/wb_physics.c src/core/wb_input.c src/core/wb_camera.c src/core/wb_character.c examples/wb_character_demo.c -o $(DEMO_CHAR) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(DEMO_CHAR)

# test-gpu: slice 11 -- the D3D12 GPU backend.
# The backend is compiled as C++ (MinGW's d3d12.h exposes C++ interfaces); the
# public header is extern "C", so the test and the rest of the engine stay C11.
CXX        := g++
TEST_GPU   := build/wb_gpu_test.exe
test-gpu: build
	gcc $(CFLAGS) -Isrc/core -c tests/wb_gpu_test.c -o build/wb_gpu_test.o
	$(CXX) -std=c++17 -O2 -Isrc/core -c src/core/wb_gpu_d3d12.c -o build/wb_gpu_d3d12.o
	$(CXX) build/wb_gpu_test.o build/wb_gpu_d3d12.o -o $(TEST_GPU) -ld3d12 -ldxgi -ld3dcompiler
	./$(TEST_GPU)

# test-assets: slice 12 -- glTF/GLB pipeline, content-addressed DDC, manifest
TEST_ASSETS := build/wb_assets_test.exe
test-assets: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_assets.c src/core/wb_json.c tests/wb_assets_test.c -o $(TEST_ASSETS) -lm
	./$(TEST_ASSETS)

# test-net: slice 13 -- server-authoritative netcode, prediction, AOI, delta
TEST_NET := build/wb_net_test.exe
test-net: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/net src/core/wb_spatial.c src/net/wb_net.c tests/wb_net_test.c -o $(TEST_NET) -lm
	./$(TEST_NET)

# test-avatar: slice 14 -- avatar rig, animation sampling, identity
TEST_AVATAR := build/wb_avatar_test.exe
test-avatar: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_assets.c src/core/wb_json.c src/core/wb_avatar.c tests/wb_avatar_test.c -o $(TEST_AVATAR) -lm
	./$(TEST_AVATAR)

# test-mod: slice 15 -- untrusted mod sandbox (manifest, isolation, budget, reload)
TEST_MOD := build/wb_mod_test.exe
test-mod: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c src/core/wb_json.c src/lua/wb_script.c src/lua/wb_mod.c tests/wb_mod_test.c $(LUA_SRC) -o $(TEST_MOD) -lm
	./$(TEST_MOD)

# test-dist: slice 16 -- instances, discovery, matchmaking, portals
TEST_DIST := build/wb_dist_test.exe
test-dist: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_dist.c tests/wb_dist_test.c -o $(TEST_DIST) -lm
	./$(TEST_DIST)

# test-edit: slice 17 -- in-world editor (edits, undo/redo, save/replay)
TEST_EDIT := build/wb_edit_test.exe
test-edit: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_ecs.c src/core/wb_json.c src/core/wb_edit.c tests/wb_edit_test.c -o $(TEST_EDIT) -lm
	./$(TEST_EDIT)

# test-mesh: slice 18 -- server meshing (authority, handover, streaming)
TEST_MESH := build/wb_mesh_test.exe
test-mesh: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/net src/net/wb_mesh.c tests/wb_mesh_test.c -o $(TEST_MESH) -lm
	./$(TEST_MESH)

clean:
	rm -rf build

# geo demo: the flying-camera / ambient-culling loop, C11-only
DEMO_OBJ := build/wb_geo_demo.exe
demo: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c src/core/wb_tiles.c examples/wb_geo_demo.c -o $(DEMO_OBJ) -lm
	./$(DEMO_OBJ)
