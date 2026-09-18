/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_LUA_GPIO_H_
#define ESPIRATE_LUA_GPIO_H_

#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the hardware GPIO devices.
 * @return 0 on success, negative errno on failure.
 */
int espirate_hw_gpio_init(void);

/**
 * @brief Register the 'gpio', 'matrix', and 'sys' modules into the Lua state.
 * @param L Lua state pointer.
 */
void luaopen_espirate_hardware(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_LUA_GPIO_H_ */
