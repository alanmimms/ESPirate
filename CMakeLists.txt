# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(espirate)

# Include directories
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/lua
)

# Lua Engine Sources
set(LUA_SOURCES
  lua/lapi.c
  lua/lauxlib.c
  lua/lbaselib.c
  lua/lcode.c
  lua/lcorolib.c
  lua/lctype.c
  lua/ldblib.c
  lua/ldebug.c
  lua/ldo.c
  lua/ldump.c
  lua/lfunc.c
  lua/lgc.c
  lua/linit.c
  lua/liolib.c
  lua/llex.c
  lua/lmathlib.c
  lua/lmem.c
  lua/loadlib.c
  lua/lobject.c
  lua/lopcodes.c
  lua/loslib.c
  lua/lparser.c
  lua/lstate.c
  lua/lstring.c
  lua/lstrlib.c
  lua/ltable.c
  lua/ltablib.c
  lua/ltm.c
  lua/lundump.c
  lua/lutf8lib.c
  lua/lvm.c
  lua/lzio.c
)

# ESPirate Application Sources
target_sources(app PRIVATE
  src/main.c
  src/shell_commands.c
  src/lua_manager.c
  src/lua_worker.c
  src/fs_manager.c
  src/lua_gpio.c
  src/posix_stubs.c
  src/setjmp.S
  ${LUA_SOURCES}
)

# Link math library
target_link_libraries(app PUBLIC -lm)

# Allow multiple definitions for standard libc/posix wrapper symbols
target_link_options(app PUBLIC "-Wl,--allow-multiple-definition")
