# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
CORE_SRC := src/core/wb_ecs.c
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

# test: build the ECS core standalone (no Lua host/main) + run the harness
TEST_TGT := build/wb_test.exe
test: build
	$(CC) $(CFLAGS) -Isrc/core src/core/wb_ecs.c tests/wb_test.c -o $(TEST_TGT)
	./$(TEST_TGT)

clean:
	rm -rf build
