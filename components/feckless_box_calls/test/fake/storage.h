#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { STORAGE_SD = 0, STORAGE_USB, STORAGE_COUNT } storage_id_t;
bool storage_present(storage_id_t id);
const char *storage_mount_path(storage_id_t id);
int storage_ff_drive(storage_id_t id);
storage_id_t storage_of_path(const char *path);
bool storage_join_path(char *out, size_t out_len, const char *dir, const char *name);
uint32_t storage_generation(void);
void storage_hold(storage_id_t id);
