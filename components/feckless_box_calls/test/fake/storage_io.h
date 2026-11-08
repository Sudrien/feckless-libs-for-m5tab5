#pragma once
typedef enum { STORAGE_IO_PLAYBACK = 0, STORAGE_IO_BACKGROUND } storage_io_class_t;
void storage_io_acquire(storage_io_class_t cls);
void storage_io_release(void);
