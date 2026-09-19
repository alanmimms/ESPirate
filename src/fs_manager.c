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
#include <esp_flash.h>
#include <esp_flash_internal.h>

extern const struct flash_area default_flash_map[];
extern const int flash_map_entries;
extern const struct flash_area *flash_map;

static struct flash_area s_dynamic_flash_map[16];
static size_t s_detected_partition_size = 0;
static uint32_t s_detected_chip_size = 0;

static void normalize_path(const char *in, char *out, size_t out_size)
{
    if (in[0] == '/') {
        strncpy(out, in, out_size - 1);
    } else {
        snprintf(out, out_size, "%s/%s", ESPIRATE_FS_MOUNT_POINT, in);
    }
    out[out_size - 1] = '\0';
}

void fs_manager_create_default_files(void)
{
    if (!is_mounted) {
        return;
    }

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
}

int fs_manager_init(void)
{
    if (is_mounted) {
        return 0;
    }

    esp_err_t ferr = esp_flash_init_default_chip();
    if (ferr != 0) {
        LOG_WRN("esp_flash_init_default_chip returned %d", ferr);
    }

    /* Detect actual physical SPI Flash size (4MB, 8MB, 16MB, etc.) */
    uint32_t chip_size = 0;
    esp_err_t sz_err = esp_flash_get_physical_size(NULL, &chip_size);
    if (sz_err != ESP_OK || chip_size == 0) {
        /* Fallback: read JEDEC ID directly (e.g. 0x684018 -> 1 << 0x18 = 16MB) */
        uint32_t flash_id = 0;
        if (esp_flash_read_id(NULL, &flash_id) == ESP_OK && flash_id > 0) {
            uint8_t size_code = flash_id & 0xFF;
            if (size_code >= 0x14 && size_code <= 0x21) { /* 1MB to 32MB */
                chip_size = 1U << size_code;
                LOG_INF("Derived physical flash size from JEDEC ID 0x%06x: %u MB (%u bytes)",
                        flash_id, chip_size / (1024 * 1024), chip_size);
            }
        }
    }

    if (chip_size > 0) {
        s_detected_chip_size = chip_size;
        /* Crucial: Update esp_flash_default_chip->size so ESP-IDF allows operations across full flash */
        if (esp_flash_default_chip != NULL) {
            esp_flash_default_chip->size = chip_size;
        }
        LOG_INF("Detected physical SPI flash size: %u MB (%u bytes)",
                chip_size / (1024 * 1024), chip_size);

        if (flash_map_entries <= ARRAY_SIZE(s_dynamic_flash_map)) {
            memcpy(s_dynamic_flash_map, default_flash_map,
                   flash_map_entries * sizeof(struct flash_area));

            uint8_t storage_id = (uint8_t)DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition));
            for (int i = 0; i < flash_map_entries; i++) {
                if (s_dynamic_flash_map[i].fa_id == storage_id) {
                    off_t start_off = s_dynamic_flash_map[i].fa_off;
                    if ((uint32_t)start_off < chip_size) {
                        size_t dynamic_size = chip_size - start_off;
                        s_dynamic_flash_map[i].fa_size = dynamic_size;
                        s_detected_partition_size = dynamic_size;
                        LOG_INF("Dynamically adapted storage_partition: offset=0x%lx, size=%u MB (%zu bytes)",
                                (unsigned long)start_off,
                                (unsigned int)(dynamic_size / (1024 * 1024)),
                                dynamic_size);
                    }
                    break;
                }
            }
            flash_map = s_dynamic_flash_map;
        }
    } else {
        LOG_WRN("Physical flash size detection returned %d, using static DTS partition size", sz_err);
    }

    int rc = fs_mount(&espirate_lfs_mount);
    if (rc < 0) {
        LOG_WRN("LittleFS mount failed (%d); formatting blank/corrupt storage partition...", rc);
#if defined(CONFIG_FILE_SYSTEM_MKFS)
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), NULL, 0);
        if (rc < 0) {
            LOG_ERR("fs_mkfs failed: %d", rc);
            return rc;
        }
        rc = fs_mount(&espirate_lfs_mount);
        if (rc < 0) {
            LOG_ERR("Failed to mount LittleFS after formatting: %d", rc);
            return rc;
        }
        LOG_INF("LittleFS partition formatted and mounted at %s", ESPIRATE_FS_MOUNT_POINT);
#else
        return rc;
#endif
    } else {
        /* Check if existing mounted filesystem size matches detected physical partition */
        struct fs_statvfs stat;
        if (fs_statvfs(ESPIRATE_FS_MOUNT_POINT, &stat) == 0 && s_detected_partition_size > 0) {
            size_t mounted_bytes = (size_t)stat.f_blocks * stat.f_frsize;
            if (mounted_bytes != s_detected_partition_size) {
                LOG_WRN("Mounted LittleFS capacity (%zu bytes) != partition size (%zu bytes). Auto-reformatting to full capacity...",
                        mounted_bytes, s_detected_partition_size);
                fs_unmount(&espirate_lfs_mount);
#if defined(CONFIG_FILE_SYSTEM_MKFS)
                rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), NULL, 0);
                if (rc == 0) {
                    rc = fs_mount(&espirate_lfs_mount);
                }
#endif
            }
        }
    }

    is_mounted = true;
    LOG_INF("LittleFS mounted successfully at %s", ESPIRATE_FS_MOUNT_POINT);

    fs_manager_create_default_files();
    return 0;
}

bool fs_manager_is_mounted(void)
{
    return is_mounted;
}

int fs_manager_format(void)
{
    int rc;
    if (is_mounted) {
        rc = fs_unmount(&espirate_lfs_mount);
        if (rc < 0) {
            LOG_WRN("fs_unmount returned %d", rc);
        }
        is_mounted = false;
    }

#if defined(CONFIG_FILE_SYSTEM_MKFS)
    rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), NULL, 0);
    if (rc < 0) {
        LOG_ERR("fs_mkfs failed: %d", rc);
        return rc;
    }
#endif

    rc = fs_mount(&espirate_lfs_mount);
    if (rc < 0) {
        LOG_ERR("fs_mount after format failed: %d", rc);
        return rc;
    }

    is_mounted = true;
    LOG_INF("LittleFS formatted and remounted at %s", ESPIRATE_FS_MOUNT_POINT);

    fs_manager_create_default_files();
    return 0;
}

int fs_manager_statvfs(size_t *total_bytes, size_t *free_bytes)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    struct fs_statvfs stat;
    int rc = fs_statvfs(ESPIRATE_FS_MOUNT_POINT, &stat);
    if (rc < 0) {
        LOG_ERR("fs_statvfs failed: %d", rc);
        return rc;
    }

    if (total_bytes) {
        *total_bytes = (size_t)stat.f_blocks * stat.f_frsize;
    }
    if (free_bytes) {
        *free_bytes = (size_t)stat.f_bfree * stat.f_frsize;
    }

    return 0;
}

int fs_manager_write_file(const char *path, const void *data, size_t len)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    char full_path[160];
    normalize_path(path, full_path, sizeof(full_path));

    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, full_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc < 0) {
        LOG_ERR("Failed to open %s for writing: %d", full_path, rc);
        return rc;
    }

    ssize_t written = fs_write(&file, data, len);
    fs_close(&file);

    if (written < 0) {
        LOG_ERR("Failed to write to %s: %d", full_path, (int)written);
        return (int)written;
    }

    return 0;
}

int fs_manager_read_file(const char *path, void *buf, size_t buf_size, size_t *bytes_read)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    char full_path[160];
    normalize_path(path, full_path, sizeof(full_path));

    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, full_path, FS_O_READ);
    if (rc < 0) {
        LOG_ERR("Failed to open %s for reading: %d", full_path, rc);
        return rc;
    }

    ssize_t read_bytes = fs_read(&file, buf, buf_size);
    fs_close(&file);

    if (read_bytes < 0) {
        LOG_ERR("Failed to read from %s: %d", full_path, (int)read_bytes);
        return (int)read_bytes;
    }

    if (bytes_read) {
        *bytes_read = (size_t)read_bytes;
    }

    return 0;
}

int fs_manager_delete_file(const char *path)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    char full_path[160];
    normalize_path(path, full_path, sizeof(full_path));

    int rc = fs_unlink(full_path);
    if (rc != 0) {
        LOG_ERR("Failed to unlink %s: %d", full_path, rc);
    }
    return rc;
}

int fs_manager_rename_file(const char *old_path, const char *new_path)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    char old_full[160];
    char new_full[160];
    normalize_path(old_path, old_full, sizeof(old_full));
    normalize_path(new_path, new_full, sizeof(new_full));

    int rc = fs_rename(old_full, new_full);
    if (rc != 0) {
        LOG_ERR("Failed to rename %s to %s: %d", old_full, new_full, rc);
    }
    return rc;
}

int fs_manager_list_files(fs_file_info_t *files, size_t max_files, size_t *count)
{
    if (!is_mounted) {
        return -ENODEV;
    }

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);

    int rc = fs_opendir(&dir, ESPIRATE_FS_MOUNT_POINT);
    if (rc != 0) {
        LOG_ERR("Failed to opendir %s: %d", ESPIRATE_FS_MOUNT_POINT, rc);
        return rc;
    }

    size_t n = 0;
    struct fs_dirent entry;
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            if (files && n < max_files) {
                strncpy(files[n].name, entry.name, sizeof(files[n].name) - 1);
                files[n].name[sizeof(files[n].name) - 1] = '\0';
                files[n].size = entry.size;
            }
            n++;
        }
    }
    fs_closedir(&dir);

    if (count) {
        *count = n;
    }
    return 0;
}

uint32_t fs_manager_get_chip_size(void)
{
    return s_detected_chip_size;
}

size_t fs_manager_get_partition_size(void)
{
    return s_detected_partition_size;
}

