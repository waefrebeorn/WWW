# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
# shared kernel: file/hash/error helpers and the glTF accessor layer
UTIL_SRC  := src/core/wb_util.c
GLTF_SRC  := src/core/wb_gltf.c
ASSETS_SRC := src/core/wb_assets.c src/core/wb_assets_gltf.c \
              src/core/wb_assets_ddc.c src/core/wb_assets_manifest.c \
              $(GLTF_SRC) $(UTIL_SRC)
AVATAR_SRC := src/core/wb_avatar.c src/core/wb_avatar_bones.c \
              src/core/wb_avatar_gltf.c src/core/wb_avatar_anim.c \
              src/core/wb_avatar_identity.c $(ASSETS_SRC)
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
test: test-ecs test-spatial test-tiles test-render test-exp test-lv test-physics test-character test-gpu test-assets test-net test-avatar test-mod test-dist test-edit test-mesh test-mode test-spawn test-worldspace

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
LV_SRC := src/lv/wb_lv_arena.c src/lv/wb_lv_value.c src/lv/wb_lv_lex.c \
          src/lv/wb_lv_parse.c src/lv/wb_lv_eval.c src/lv/wb_lv_host.c
TEST_LV  := build/wb_lv_test.exe
TEST_LVS := build/wb_lv_script_test.exe
test-lv: test-lv-core test-lv-script
test-lv-core: build
	$(CC) $(CFLAGS) -Isrc/lv $(LV_SRC) tests/wb_lv_test.c -o $(TEST_LV) -lm
	./$(TEST_LV)
test-lv-script: build
	$(CC) $(CFLAGS) -Isrc/lv $(LV_SRC) tests/wb_lv_script_test.c -o $(TEST_LVS) -lm
	./$(TEST_LVS)

# test-physics: slice 9 -- MuJoCo-backed physics world
MUJOCO_DIR  := thirdparty/mujoco
PHYS_SRC    := src/core/wb_physics_world.c src/core/wb_physics_body.c \
               src/core/wb_physics_bind.c
TEST_PHYSICS := build/wb_physics_test.exe
test-physics: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c $(PHYS_SRC) tests/wb_physics_test.c -o $(TEST_PHYSICS) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(TEST_PHYSICS)

# test-character: slice 10 -- input + camera + character controller chain
TEST_CHAR := build/wb_character_test.exe
test-character: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c $(PHYS_SRC) src/core/wb_input.c src/core/wb_camera.c src/core/wb_character.c tests/wb_character_test.c -o $(TEST_CHAR) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(TEST_CHAR)

# demo-character: slice 10 headless playable-character demo
DEMO_CHAR := build/wb_character_demo.exe
demo-character: build
	cmd /c copy /Y thirdparty\mujoco\bin\mujoco.dll build\mujoco.dll
	$(CC) $(CFLAGS) -Isrc/core -I$(MUJOCO_DIR)/include src/core/wb_ecs.c $(PHYS_SRC) src/core/wb_input.c src/core/wb_camera.c src/core/wb_character.c examples/wb_character_demo.c -o $(DEMO_CHAR) -L$(MUJOCO_DIR)/lib -lmujoco -lm
	./$(DEMO_CHAR)

# test-gpu: slice 11 -- the D3D12 GPU backend.
# The backend is compiled as C++ (MinGW's d3d12.h exposes C++ interfaces); the
# public header is extern "C", so the test and the rest of the engine stay C11.
CXX        := g++
GPU_SRC := src/core/wb_gpu_dev.c src/core/wb_gpu_buffer.c \
           src/core/wb_gpu_texture.c src/core/wb_gpu_shader.c \
           src/core/wb_gpu_pass.c src/core/wb_gpu_exec.c
GPU_OBJS := build/wb_gpu_dev.o build/wb_gpu_buffer.o build/wb_gpu_texture.o \
            build/wb_gpu_shader.o build/wb_gpu_pass.o build/wb_gpu_exec.o
TEST_GPU   := build/wb_gpu_test.exe
build/wb_gpu_%.o: src/core/wb_gpu_%.c
	$(CXX) -std=c++17 -O2 -Isrc/core -c $< -o $@
test-gpu: build $(GPU_OBJS)
	gcc $(CFLAGS) -Isrc/core -c tests/wb_gpu_test.c -o build/wb_gpu_test.o
	$(CXX) build/wb_gpu_test.o $(GPU_OBJS) -o $(TEST_GPU) -ld3d12 -ldxgi -ld3dcompiler
	./$(TEST_GPU)

# test-assets: slice 12 -- glTF/GLB pipeline, content-addressed DDC, manifest
TEST_ASSETS := build/wb_assets_test.exe
test-assets: build
	$(CC) $(CFLAGS) -Isrc/core $(ASSETS_SRC) src/core/wb_json.c tests/wb_assets_test.c -o $(TEST_ASSETS) -lm
	./$(TEST_ASSETS)

# test-net: slice 13 -- server-authoritative netcode, prediction, AOI, delta
NET_SRC := src/net/wb_net_aoi.c src/net/wb_net_server.c \
           src/net/wb_net_client.c src/net/wb_net_loopback.c
MODE_SRC := src/core/wb_mode.c $(UTIL_SRC) src/core/wb_json.c
SPAWN_SRC := src/core/wb_spawn.c
WORLDSPACE_SRC := src/core/wb_worldspace.c src/core/wb_mode.c \
                  src/core/wb_spawn.c src/core/wb_dist.c \
                  $(UTIL_SRC) src/core/wb_json.c
TEST_NET := build/wb_net_test.exe
test-net: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/net src/core/wb_spatial.c $(NET_SRC) tests/wb_net_test.c -o $(TEST_NET) -lm
	./$(TEST_NET)

# test-avatar: slice 14 -- avatar rig, animation sampling, identity
TEST_AVATAR := build/wb_avatar_test.exe
test-avatar: build
	$(CC) $(CFLAGS) -Isrc/core $(AVATAR_SRC) src/core/wb_json.c tests/wb_avatar_test.c -o $(TEST_AVATAR) -lm
	./$(TEST_AVATAR)

# test-mod: slice 15 -- untrusted mod sandbox (manifest, isolation, budget, reload)
TEST_MOD := build/wb_mod_test.exe
test-mod: build
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c src/core/wb_json.c $(UTIL_SRC) src/lua/wb_script.c src/lua/wb_mod.c tests/wb_mod_test.c $(LUA_SRC) -o $(TEST_MOD) -lm
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

# test-mode: slice 19 -- gametype manifests + config schema
TEST_MODE := build/wb_mode_test.exe
test-mode: build
	$(CC) $(CFLAGS) -Isrc/core $(MODE_SRC) tests/wb_mode_test.c -o $(TEST_MODE) -lm
	./$(TEST_MODE)

# test-spawn: slice 19 -- spawn points
TEST_SPAWN := build/wb_spawn_test.exe
test-spawn: build
	$(CC) $(CFLAGS) -Isrc/core $(SPAWN_SRC) tests/wb_spawn_test.c -o $(TEST_SPAWN) -lm
	./$(TEST_SPAWN)

# test-worldspace: slice 19 -- the spawn-a-worldspace facade
TEST_WS := build/wb_worldspace_test.exe
test-worldspace: build
	$(CC) $(CFLAGS) -Isrc/core $(WORLDSPACE_SRC) tests/wb_worldspace_test.c -o $(TEST_WS) -lm
	./$(TEST_WS)

clean:
	rm -rf build

# geo demo: the flying-camera / ambient-culling loop, C11-only
DEMO_OBJ := build/wb_geo_demo.exe
demo: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c src/core/wb_tiles.c examples/wb_geo_demo.c -o $(DEMO_OBJ) -lm
	./$(DEMO_OBJ)
