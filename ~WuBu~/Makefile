# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LDLIBS  := -lm -lpthread
# Pin make's command shell to the Git bash. Make auto-detects Hermes's
# bundled sh.exe (WindowsApps, Smart App Control-blocked) and dies with
# 'Access is denied' on the first gate. This is environmental, not code.
SHELL   := C:/Program Files/Git/bin/sh.exe
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
# Lua 5.4 is compiled once into objects and linked; recompiling 34 files for
# every gate that needs it was the bulk of the suite's build time.
LUA_OBJS := $(patsubst thirdparty/lua5.4/%.c,build/lua54_%.o,$(LUA_SRC))
LUA_INC := -Ithirdparty/lua5.4

build/lua54_%.o: thirdparty/lua5.4/%.c
	$(CC) $(CFLAGS) $(LUA_INC) -c $< -o $@
# shared kernel: file/hash/error helpers and the glTF accessor layer
UTIL_SRC  := src/core/wb_util.c
GLTF_SRC  := src/core/wb_gltf.c
ASSETS_SRC := src/core/wb_assets.c src/core/wb_assets_gltf.c \
              src/core/wb_assets_ddc.c src/core/wb_assets_manifest.c \
              $(GLTF_SRC) $(UTIL_SRC)
AVATAR_SRC := src/core/wb_avatar.c src/core/wb_avatar_bones.c \
              src/core/wb_avatar_gltf.c src/core/wb_avatar_anim.c \
              src/core/wb_avatar_identity.c $(ASSETS_SRC)
CORE_SRC := src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_tiles.c src/core/wb_render.c src/core/wb_exp.c src/core/wb_host.c src/core/wb_prof.c src/core/wb_det.c src/core/wb_snap.c src/core/wb_interp.c src/core/wb_job.c src/core/wb_pool.c
SCRIPT_SRC := src/lua/wb_script.c src/lua/wb_space_bind.c
HOST_SRC := src/lua/wb_lua_host.c
OBJ     := build/lua.o build/wb_ecs.o build/wb_host.o
TARGET  := build/wubu_engine.exe

.PHONY: all clean run test demo

all: $(TARGET)

build:
	mkdir -p build

$(TARGET): build $(CORE_SRC) $(SCRIPT_SRC) $(HOST_SRC) $(LUA_OBJS) $(WORLDSPACE_SRC)
	$(CC) $(CFLAGS) -Isrc/lua $(CORE_SRC) $(SCRIPT_SRC) $(HOST_SRC) $(WORLDSPACE_SRC) $(LUA_OBJS) -o $@ $(LDLIBS)

run: $(TARGET)
	./$(TARGET) games omniverse-hub

# run-mode: run any gametype package under games/ -- `make run-mode M=duel-stadium`
run-mode: $(TARGET)
	./$(TARGET) games $(M)

# test: build each core module standalone (no Lua host/main) + run its harness
# NOTE (environment): Windows Smart App Control intermittently blocks a freshly
# linked test exe with "An Application Control policy has blocked this file"
# (make e=4551). It is not a code failure. Workaround: delete the named exe and
# re-run -- the next link usually passes. A full-suite run may need several
# delete-and-retry rounds on a fresh checkout.
test: test-ecs test-spatial test-tiles test-render test-exp test-lv test-physics test-character test-gpu test-assets test-net test-avatar test-mod test-dist test-edit test-mesh test-mode test-spawn test-worldspace test-portal test-space test-host test-prof test-det test-snap test-math test-fastmath test-interp test-job test-pool

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
test-exp: build $(LUA_OBJS)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_exp.c src/core/wb_prof.c src/core/wb_det.c src/core/wb_snap.c src/core/wb_interp.c src/lua/wb_script.c tests/wb_exp_test.c $(LUA_OBJS) -o $(TEST_EXP) -lm
	./$(TEST_EXP)

# test-lv: slerm-Lua -- the Wua submodule (~WuBu~/src/lv, github.com/waefrebeorn/Wua)
test-lv: build
	$(MAKE) -C src/lv test

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
# wb_mod now installs the space bindings, so it links the worldspace stack.
TEST_MOD := build/wb_mod_test.exe
test-mod: build $(LUA_OBJS)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c \
	  $(WORLDSPACE_SRC) src/lua/wb_script.c src/lua/wb_mod.c src/lua/wb_space_bind.c \
	  tests/wb_mod_test.c $(LUA_OBJS) -o $(TEST_MOD) -lm
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

# test-portal: slice 20 -- the Yu-Gi-Oh stadium mechanic (portal travel)
TEST_PORTAL := build/wb_portal_test.exe
test-portal: build
	$(CC) $(CFLAGS) -Isrc/core $(WORLDSPACE_SRC) tests/wb_portal_test.c -o $(TEST_PORTAL) -lm
	./$(TEST_PORTAL)

# test-space: slice 21 -- the gamemode-facing space (gates/travel/instances
# from inside the Lua sandbox). Needs the mod sandbox + the worldspace core.
TEST_SPACE := build/wb_space_test.exe
test-space: build $(LUA_OBJS)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c \
	  $(WORLDSPACE_SRC) src/lua/wb_script.c src/lua/wb_mod.c \
	  src/lua/wb_space_bind.c tests/wb_space_test.c \
	  $(LUA_OBJS) -o $(TEST_SPACE) -lm
	./$(TEST_SPACE)

# test-host: slice 22 -- the gametype host: scan a directory of gametypes,
# register them, run the named mode's script, spawn the player, run the world.
TEST_HOST := build/wb_host_test.exe
test-host: build $(LUA_OBJS)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/lua src/core/wb_ecs.c \
	  $(WORLDSPACE_SRC) src/core/wb_host.c src/lua/wb_script.c \
	  src/lua/wb_space_bind.c tests/wb_host_test.c \
	  $(LUA_OBJS) -o $(TEST_HOST) -lm
	./$(TEST_HOST)

# test-prof: LP-01 stone 1 -- frame profiler (counters, zones, ring buffer)
TEST_PROF := build/wb_prof_test.exe
test-prof: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_prof.c tests/wb_prof_test.c -o $(TEST_PROF)
	./$(TEST_PROF)

# test-det: LP-03 stone 1 -- deterministic substrate (splitmix64 PRNG, FNV-1a)
TEST_DET := build/wb_det_test.exe
test-det: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_det.c tests/wb_det_test.c -o $(TEST_DET)
	./$(TEST_DET)

# test-snap: LP-03 stone 3 -- per-tick world digest over stable entity order
TEST_SNAP := build/wb_snap_test.exe
test-snap: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_ecs.c src/core/wb_snap.c src/core/wb_det.c tests/wb_snap_test.c -o $(TEST_SNAP)
	./$(TEST_SNAP)

# test-math: LP-03 -- deterministic fixed-point + integer CORDIC trig
TEST_MATH := build/wb_math_test.exe
test-math: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_math.c tests/wb_math_test.c -o $(TEST_MATH) -lm
	./$(TEST_MATH)

# test-fastmath: LP-08 seed -- folded sin/cos (slermed from wubuwizard foldmath)
TEST_FASTMATH := build/wb_fastmath_test.exe
test-fastmath: build
	$(CC) $(CFLAGS) -Isrc/core -mavx2 -mfma tests/wb_fastmath_test.c -o $(TEST_FASTMATH) -lm
	./$(TEST_FASTMATH)

# test-interp: LP-04 -- fixed-timestep render interpolation
TEST_INTERP := build/wb_interp_test.exe
test-interp: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_interp.c src/core/wb_det.c tests/wb_interp_test.c -o $(TEST_INTERP)
	./$(TEST_INTERP)

# test-job: LP-06 stone 1 -- the deterministic job graph
TEST_JOB := build/wb_job_test.exe
test-job: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_job.c tests/wb_job_test.c -o $(TEST_JOB)
	./$(TEST_JOB)

# test-pool: LP-06 stone 2 -- the worker pool (deterministic parallel-for)
TEST_POOL := build/wb_pool_test.exe
test-pool: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_pool.c tests/wb_pool_test.c -o $(TEST_POOL) $(LDLIBS)
	./$(TEST_POOL)

clean:
	rm -rf build

# geo demo: the flying-camera / ambient-culling loop, C11-only
DEMO_OBJ := build/wb_geo_demo.exe
demo: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_spatial.c src/core/wb_tiles.c examples/wb_geo_demo.c -o $(DEMO_OBJ) -lm
	./$(DEMO_OBJ)
