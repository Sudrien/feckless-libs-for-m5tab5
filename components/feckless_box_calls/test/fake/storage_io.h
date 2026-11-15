#pragma once
typedef enum { STORAGE_IO_PLAYBACK = 0, STORAGE_IO_BACKGROUND } storage_io_class_t;
void storage_io_acquire(storage_io_class_t cls);
void storage_io_release(void);
/* u-feckless-0047 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
FILE *storage_io_open(const char *path, const char *mode);
int storage_io_close(FILE *f);
size_t storage_io_fread(void *dst, size_t len, FILE *f, storage_io_class_t cls);
bool storage_io_read_at(FILE *f, int64_t off, void *dst, size_t len, storage_io_class_t cls);
uint32_t storage_io_phase_ms(void);
void storage_io_report(const char *phase);
