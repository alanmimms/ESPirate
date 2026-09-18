/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bridge POSIX file I/O calls from picolibc to Zephyr's fs subsystem.
 * This enables full Lua io.*, os.remove/rename, and dofile() support.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>

#define MAX_FDS 16

static struct fs_file_t s_open_files[MAX_FDS];
static bool s_file_active[MAX_FDS] = {false};
K_MUTEX_DEFINE(s_fd_mutex);

int open(const char *path, int flags, ...)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    uint8_t zflags = 0;
    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY) {
        zflags |= FS_O_READ;
    } else if (acc == O_WRONLY) {
        zflags |= FS_O_WRITE;
    } else if (acc == O_RDWR) {
        zflags |= (FS_O_READ | FS_O_WRITE);
    }

    if (flags & O_CREAT) {
        zflags |= FS_O_CREATE;
    }
    if (flags & O_TRUNC) {
        zflags |= FS_O_TRUNC;
    }
    if (flags & O_APPEND) {
        zflags |= FS_O_APPEND;
    }

    k_mutex_lock(&s_fd_mutex, K_FOREVER);
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!s_file_active[i]) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        k_mutex_unlock(&s_fd_mutex);
        errno = EMFILE;
        return -1;
    }

    fs_file_t_init(&s_open_files[fd]);
    int rc = fs_open(&s_open_files[fd], path, zflags);
    if (rc < 0) {
        k_mutex_unlock(&s_fd_mutex);
        errno = -rc;
        return -1;
    }

    s_file_active[fd] = true;
    k_mutex_unlock(&s_fd_mutex);
    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (fd < 3 || fd >= MAX_FDS || !s_file_active[fd]) {
        errno = EBADF;
        return -1;
    }

    ssize_t rc = fs_read(&s_open_files[fd], buf, count);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return rc;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (fd == 1 || fd == 2) {
        /* Direct stdout/stderr to printk */
        const char *cbuf = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            printk("%c", cbuf[i]);
        }
        return (ssize_t)count;
    }

    if (fd < 3 || fd >= MAX_FDS || !s_file_active[fd]) {
        errno = EBADF;
        return -1;
    }

    ssize_t rc = fs_write(&s_open_files[fd], buf, count);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return rc;
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (fd < 3 || fd >= MAX_FDS || !s_file_active[fd]) {
        errno = EBADF;
        return -1;
    }

    int zwhence = FS_SEEK_SET;
    if (whence == SEEK_CUR) {
        zwhence = FS_SEEK_CUR;
    } else if (whence == SEEK_END) {
        zwhence = FS_SEEK_END;
    }

    int rc = fs_seek(&s_open_files[fd], offset, zwhence);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }

    off_t pos = fs_tell(&s_open_files[fd]);
    if (pos < 0) {
        errno = -pos;
        return -1;
    }
    return pos;
}

int close(int fd)
{
    if (fd < 3 || fd >= MAX_FDS || !s_file_active[fd]) {
        errno = EBADF;
        return -1;
    }

    k_mutex_lock(&s_fd_mutex, K_FOREVER);
    int rc = fs_close(&s_open_files[fd]);
    s_file_active[fd] = false;
    k_mutex_unlock(&s_fd_mutex);

    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

int unlink(const char *pathname)
{
    if (!pathname) {
        errno = EINVAL;
        return -1;
    }
    int rc = fs_unlink(pathname);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

int rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }
    int rc = fs_rename(oldpath, newpath);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

clock_t times(void *buf)
{
    (void)buf;
    return (clock_t)k_uptime_get();
}
