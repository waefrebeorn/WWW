# WuBu Engine -- build
CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Isrc/core -Ithirdparty/lua5.4
LUA_SRC := $(filter-out %/lua.c %/onelua.c %/ltests.c,$(wildcard thirdparty/lua5.4/*.c))
CORE_SRC := src/core/wb_ecs.c
HOST_SRC := src/lua/wb_lua_host.c
OBJ     := build/lua.o build/wb_ecs.o build/wb_host.o
TARGET  := build/wubu_engine.exe

.PHONY: all clean run

all: $(TARGET)

build:
	mkdir -p build

$(TARGET): build $(CORE_SRC) $(HOST_SRC) $(LUA_SRC)
	$(CC) $(CFLAGS) $(CORE_SRC) $(HOST_SRC) $(LUA_SRC) -o $@ -lm

run: $(TARGET)
	./$(TARGET) games/main.lua

clean:
	rm -rf build
