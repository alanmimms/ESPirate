/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>

int __attribute__((weak)) open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

ssize_t __attribute__((weak)) read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    errno = ENOSYS;
    return -1;
}

ssize_t __attribute__((weak)) write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    errno = ENOSYS;
    return -1;
}

off_t __attribute__((weak)) lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    errno = ENOSYS;
    return -1;
}

int __attribute__((weak)) close(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

int __attribute__((weak)) unlink(const char *pathname)
{
    (void)pathname;
    errno = ENOSYS;
    return -1;
}

int __attribute__((weak)) rename(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

clock_t __attribute__((weak)) times(void *buf)
{
    (void)buf;
    return (clock_t)-1;
}
