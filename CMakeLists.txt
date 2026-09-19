# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

set(ENV{ZEPHYR_BASE} "/home/alan/zephyr-projects/zephyr")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(espirate)

# Web Dashboard Generator: converts web/index.html -> web_dashboard.h
set(WEB_HTML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html)
set(GEN_WEB_HEADER ${CMAKE_CURRENT_BINARY_DIR}/include/web_dashboard.h)
set(GEN_WEB_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_web_dashboard.py)

# Generate at CMake configure time if not already present or out of date
execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${GEN_WEB_SCRIPT} ${WEB_HTML_FILE} ${GEN_WEB_HEADER}
)

# Custom command to regenerate during build whenever web/index.html or the generator changes
add_custom_command(
  OUTPUT ${GEN_WEB_HEADER}
  COMMAND ${PYTHON_EXECUTABLE} ${GEN_WEB_SCRIPT} ${WEB_HTML_FILE} ${GEN_WEB_HEADER}
  DEPENDS ${WEB_HTML_FILE} ${GEN_WEB_SCRIPT}
  COMMENT "Generating web_dashboard.h from ${WEB_HTML_FILE}"
  VERBATIM
)

# Custom target ensuring header is generated before any target compiles
add_custom_target(generate_web_dashboard DEPENDS ${GEN_WEB_HEADER})
add_dependencies(app generate_web_dashboard)

# Include directories
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/lua
  ${CMAKE_CURRENT_BINARY_DIR}/include
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
  src/wifi_manager.c
  src/captive_dns.c
  src/web_server.c
  src/posix_stubs.c
  src/setjmp.S
  ${GEN_WEB_HEADER}
  ${LUA_SOURCES}
)

# Link math library
target_link_libraries(app PUBLIC -lm)

# Allow multiple definitions for standard libc/posix wrapper symbols
target_link_options(app PUBLIC "-Wl,--allow-multiple-definition")
