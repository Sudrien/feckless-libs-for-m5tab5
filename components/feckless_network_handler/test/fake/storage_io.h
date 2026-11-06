/*
 * Host stand-in for feckless_storage_handler's storage_io.h, for
 * fetchtest (9003). The lease is a mutex, and the fake checks the rules
 * a caller must keep: never acquired twice by one task, never held while
 * waiting on the network (fake esp_http_client_read() fails the test).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdio.h>
typedef enum {
    STORAGE_IO_PLAYBACK   = 0,
    STORAGE_IO_PREFETCH   = 1,
    STORAGE_IO_BACKGROUND = 2,
    STORAGE_IO_CLASSES
} storage_io_class_t;
#define STORAGE_IO_CHUNK (16 * 1024)
FILE *storage_io_open(const char *path, const char *mode);
int   storage_io_close(FILE *f);
void  storage_io_acquire(storage_io_class_t cls);
void  storage_io_release(void);
