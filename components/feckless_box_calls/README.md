# feckless_box_calls

What a launcher answers an app in a feckless box with (the `umode` branch,
u-feckless-0006): the call table `feckless_umode_load()` takes.

```c
uint32_t n;
const feckless_umode_call_t *calls = feckless_box_calls_v1(&n);
feckless_umode_load(image, size, calls, n, 4 << 20, 64 << 10, &app);
```

## What is in it

- **The box's heap**: `malloc`, `calloc`, `realloc`, `free`,
  `heap_caps_malloc` (`feckless_umode`).
- **The version symbols** `feckless_abi_v1` and `feckless_abi_box_v1`, as
  data copied into the box.
- **libgcc's value helpers** that cannot link into an app: double `+ - * /`,
  64-bit division and remainder (with RISC-V's answers for division by zero
  and `INT64_MIN / -1`), int-to-double conversions, `clz`/`ctz`. The ones
  that pass a `float` in `fa0` are not here (the trap does not save `fa0`):
  `__extendsfdf2`, `__fixsfdi`, `__fixunssfdi`, `__floatdisf`,
  `__floatundisf`. An app needing one fails the load by name.
- **Console and time**: `putchar`, `puts`, `vTaskDelay`,
  `esp_timer_get_time`, `esp_log_timestamp`, `esp_rom_delay_us` (capped at
  100 ms a call), `time`, `gettimeofday`.
- **The log line**: `feckless_box_log`, which `feckless_box_libc`'s
  `esp_log()` and `esp_log_write()` call with the line already formatted in
  the app.
- **The board**: `tab5io_init`, `tab5io_bus`, `lcd_init`, `gfx_init`,
  `touch_init`, `lcd_backlight_set`, `touch_get`. The app gets numbers for the
  panel and the bus, never the host's pointers.
- **gfx**: `gfx_fill_rect`, `gfx_fill_circle`, `gfx_blit`, `gfx_draw_text`,
  `gfx_text_w`, `gfx_wrap_line`, `gfx_draw_para` (eight arguments: the
  eighth comes through `t0`).

- **Storage** (u-feckless-0007), as Spurious uses it:
  - FatFs `f_open`, `f_read`, `f_lseek`, `f_close`. The real `FIL` is the
    host's, found by the address of the app's; after each call only what
    `f_size`, `f_tell`, `f_eof` and `f_error` read is copied back, never the
    `FATFS` pointer. Fast seek works: the host builds its own cluster table
    and tells the app the count FatFs puts in the first word.
  - stdio `fopen`, `fread`, `fgets`, `fseek`, `ftell`, `fclose`: the app
    gets a number for each `FILE`.
  - `opendir`, `readdir`, `closedir`: a number for each `DIR`, and
    `readdir()`'s `struct dirent` copied into the box.
  - `storage_present`, `storage_ff_drive`, `storage_generation`,
    `storage_of_path`, `storage_mount_path` (a copy in the box),
    `storage_join_path`, `storage_io_acquire`/`_release`, `storage_hold`.
  - `feckless_box_calls_end()`, after `feckless_umode_unload()`: closes what
    the app left open and gives back its I/O lease and volume hold, which
    the launcher's own storage would otherwise wait on.

  Not yet: writing (`f_write`, `fwrite`), `f_stat`, `f_opendir`, `open`/`read`,
  `access`, `fseeko`, the storage list; the storage section's other names.

## Tests

    make -C components/feckless_box_calls/test

Every call through `feckless_umode`'s ext hook as a box makes it, against
recording stand-ins for the board; and storage as Spurious calls it, against
a stand-in FatFs and storage handler over real files, and the host's own
stdio and directories.
