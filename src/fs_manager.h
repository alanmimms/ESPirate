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

typedef struct {
    char name[64];
    size_t size;
} fs_file_info_t;

/**
 * @brief Initialize and mount the LittleFS partition at /lfs.
 * If mounting fails (e.g. on fresh chip), auto-formats the partition.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_init(void);

/**
 * @brief Check if the LittleFS partition is currently mounted.
 * @return true if mounted, false otherwise.
 */
bool fs_manager_is_mounted(void);

/**
 * @brief Reformat the LittleFS partition and recreate default files.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_format(void);

/**
 * @brief Get filesystem capacity and free space statistics.
 * @param total_bytes Pointer to store total capacity in bytes.
 * @param free_bytes Pointer to store free space in bytes.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_statvfs(size_t *total_bytes, size_t *free_bytes);

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

/**
 * @brief Delete (unlink) a file in LittleFS.
 * @param path File path or filename (e.g. "test.lua" or "/lfs/test.lua").
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_delete_file(const char *path);

/**
 * @brief Rename a file in LittleFS.
 * @param old_path Current path or filename.
 * @param new_path Target path or filename.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_rename_file(const char *old_path, const char *new_path);

/**
 * @brief List files in LittleFS mount point.
 * @param files Buffer to store file info structures (may be NULL if max_files is 0).
 * @param max_files Maximum number of entries to store.
 * @param count Pointer to receive actual total count found.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_list_files(fs_file_info_t *files, size_t max_files, size_t *count);

/**
 * @brief Ensure default system files exist (e.g. /lfs/demo.lua and /lfs/howto.md).
 */
void fs_manager_create_default_files(void);

/**
 * @brief Force restore /lfs/howto.md from compiled-in firmware documentation.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_restore_docs(void);

/**
 * @brief Force restore /lfs/index.html from compiled-in firmware dashboard.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_restore_web(void);

/**
 * @brief Force restore /lfs/favicon.png from compiled-in firmware image.
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_restore_favicon(void);

/**
 * @brief Force restore all default files (demo.lua, howto.md, index.html, favicon.png).
 * @return 0 on success, negative error code on failure.
 */
int fs_manager_restore_all(void);

/**
 * @brief Get detected physical SPI flash chip size in bytes.
 * @return Detected physical size in bytes (e.g. 16777216 for 16MB), or 0 if undetected.
 */
uint32_t fs_manager_get_chip_size(void);

/**
 * @brief Get dynamically adapted LittleFS partition size in bytes.
 * @return Partition size in bytes, or 0 if undetected.
 */
size_t fs_manager_get_partition_size(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_FS_MANAGER_H_ */
