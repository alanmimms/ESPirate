/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "fs_manager.h"

LOG_MODULE_REGISTER(fs_manager, LOG_LEVEL_INF);

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage_data);

static struct fs_mount_t espirate_lfs_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_data,
    .storage_dev = (void *)DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)),
    .mnt_point = ESPIRATE_FS_MOUNT_POINT,
};

static bool is_mounted = false;

#include <esp_err.h>
#include <esp_flash_internal.h>

int fs_manager_init(void)
{
    if (is_mounted) {
        return 0;
    }

    esp_err_t ferr = esp_flash_init_default_chip();
    if (ferr != 0) {
        LOG_WRN("esp_flash_init_default_chip returned %d", ferr);
    }

    int rc = fs_mount(&espirate_lfs_mount);
    if (rc < 0) {
        LOG_ERR("Failed to mount LittleFS at %s: %d", ESPIRATE_FS_MOUNT_POINT, rc);
        return rc;
    }

    is_mounted = true;
    LOG_INF("LittleFS mounted successfully at %s", ESPIRATE_FS_MOUNT_POINT);

    /* Write default demo script if it doesn't exist yet */
    struct fs_dirent dirent;
    if (fs_stat(ESPIRATE_FS_MOUNT_POINT "/demo.lua", &dirent) != 0) {
        const char *demo_code =
            "-- ESPirate LittleFS Demo Script\n"
            "print('========================================')\n"
            "print('Hello from /lfs/demo.lua!')\n"
            "print('Executing from persistent flash storage')\n"
            "print('Uptime ms:', sys.uptime())\n"
            "for i = 1, 3 do\n"
            "    print('  Iteration', i, 'running...')\n"
            "    sys.sleep(50)\n"
            "end\n"
            "print('Demo completed successfully.')\n"
            "print('========================================')\n";
        fs_manager_write_file(ESPIRATE_FS_MOUNT_POINT "/demo.lua", demo_code, strlen(demo_code));
        LOG_INF("Created default /lfs/demo.lua");
    }

    return 0;
}

bool fs_manager_is_mounted(void)
{
    return is_mounted;
}

int fs_manager_write_file(const char *path, const void *data, size_t len)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc < 0) {
        LOG_ERR("Failed to open %s for writing: %d", path, rc);
        return rc;
    }

    ssize_t written = fs_write(&file, data, len);
    fs_close(&file);

    if (written < 0) {
        LOG_ERR("Failed to write to %s: %d", path, (int)written);
        return (int)written;
    }

    return 0;
}

int fs_manager_read_file(const char *path, void *buf, size_t buf_size, size_t *bytes_read)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, path, FS_O_READ);
    if (rc < 0) {
        LOG_ERR("Failed to open %s for reading: %d", path, rc);
        return rc;
    }

    ssize_t read_bytes = fs_read(&file, buf, buf_size);
    fs_close(&file);

    if (read_bytes < 0) {
        LOG_ERR("Failed to read from %s: %d", path, (int)read_bytes);
        return (int)read_bytes;
    }

    if (bytes_read) {
        *bytes_read = (size_t)read_bytes;
    }

    return 0;
}
