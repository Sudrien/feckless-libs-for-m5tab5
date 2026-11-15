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
/* u-feckless-0047 */
const char *storage_label(storage_id_t id);
bool storage_is_hidden(const char *name);
void storage_mark_hidden(const char *path);
typedef struct { bool present; char name[32]; char type[16]; uint64_t capacity_mb; int speed_khz; int bus_width; } storage_sd_info_t;
typedef struct { bool present; bool powered; char product[36]; char manufacturer[36]; uint16_t vid; uint16_t pid; uint64_t capacity_mb; uint32_t sector_size; } storage_usb_info_t;
bool storage_usb_power(bool on);
bool storage_usb_busy(void);
bool storage_usb_powered(void);
void storage_sd_info(storage_sd_info_t *out);
void storage_usb_info(storage_usb_info_t *out);
void storage_hold_background(storage_id_t id);
void storage_hold_readers(uint32_t mask);
void storage_hold_brief(storage_id_t id);
void storage_release_brief(storage_id_t id);
