/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_FS_MANAGER_H_
#define ESPIRATE_FS_MANAGER_H_

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPIRATE_FS_MOUNT_POINT "/lfs"

/**
 * @brief Initialize and mount the LittleFS partition at /lfs.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_init(void);

/**
 * @brief Check if the LittleFS partition is currently mounted.
 * @return true if mounted, false otherwise.
 */
bool fs_manager_is_mounted(void);

/**
 * @brief Write data to a file in LittleFS.
 * @param path Full file path (e.g. "/lfs/test.lua").
 * @param data Buffer containing file content.
 * @param len Length of data in bytes.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_write_file(const char *path, const void *data, size_t len);

/**
 * @brief Read data from a file in LittleFS.
 * @param path Full file path.
 * @param buf Output buffer.
 * @param buf_size Size of output buffer.
 * @param bytes_read Pointer to store actual bytes read (optional).
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_read_file(const char *path, void *buf, size_t buf_size, size_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_FS_MANAGER_H_ */
