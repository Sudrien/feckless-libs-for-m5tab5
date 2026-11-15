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
  `__floatundisf`; `feckless_box_libc` links them into the app
  (u-feckless-0027).
- **Console and time**: `putchar`, `puts`, `vTaskDelay`,
  `esp_timer_get_time`, `esp_log_timestamp`, `esp_rom_delay_us` (capped at
  100 ms a call), `time`, `gettimeofday`.
- **The log line**: `feckless_box_log`, which `feckless_box_libc`'s
  `esp_log()` and `esp_log_write()` call with the line already formatted in
  the app.
- **The board**: `tab5io_init`, `tab5io_bus`, `lcd_init`, `gfx_init`,
  `touch_init`, `lcd_backlight_set`, `touch_get`. The app gets numbers for the
  panel and the bus, never the host's pointers.
- **Threads** (u-feckless-0023): `xTaskCreatePinnedToCore` (and so IDF's
  inline `xTaskCreate`), each thread a window of its own on a host task,
  with its stack in the box (`feckless_umode_app_spawn()`). Calls run under
  the app's lock, one thread at a time, except those flagged UNLOCKED: the
  delays and clocks, the log line, libgcc's value helpers, and the audio
  reads and writes that wait and the queries that only read a flag.
- **The framebuffer in place** (u-feckless-0022): `gfx_fb`, `gfx_w`,
  `gfx_h`. `gfx_fb()` returns the host's buffer and grants the app that
  range, read and write but never execute, on top of its box, until it is
  unloaded (`feckless_umode_app_grant()`). Host calls still take pointers
  into the box only.
- **More of the board** (u-feckless-0021): `tab5io_exp1`, `tab5io_exp2`
  (numbers again), `touch_present`, `touch_set_rotation`.
- **Ark12's tables** (u-feckless-0021) as data, for `ark12.h`'s inline
  `ark12_glyph()`: `ark12_runs`, `ark12_flags`, `ark12_ckpt`,
  `ark12_litbase`, `ark12_lit`, `ark12_tiles`, about 380 KB, copied into the
  box only if the app imports them.
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

  - u-feckless-0047, as Defeatist uses it: `fwrite` (through the staging
    buffer, as reads are), `fflush`, `rewind`, `remove`, `rename`, `mkdir`,
    `rmdir`, `stat`, `utime`, `rewinddir`; FatFs's `f_opendir`,
    `f_readdir`, `f_closedir` (the host's `FF_DIR`, found by the app's)
    and `f_chmod`; `storage_io_open`, `_close`, `_fread`, `_read_at`
    (staged, under storage_io's lease), `_phase_ms`, `_report`;
    `storage_label`, `_is_hidden`, `_mark_hidden`, `_sd_info`, `_usb_info`,
    `_usb_busy`, `_usb_power`, `_usb_powered`, `_hold_background`,
    `_hold_readers`, `_hold_brief`, `_release_brief`. Sixteen FILEs, four
    FatFs directories. The storage list is not here: it calls the app's
    filter, so an app links `storage_list.c` into itself, over these.

  - u-feckless-0050, as Jinxed Writer's log uses it: `ferror`, `feof`,
    `clearerr`, `fileno` (a number of its own, which only `fsync` takes),
    `fsync`, `unlink`. A failed stdio or path call now sets the app's
    `errno` -- the word at the thread's `tp` -- from the host's, or
    `EFAULT`, `EBADF` or `EMFILE` for a refusal before any call.

  Not yet: `f_write`, `f_stat`, `open`/`read`, `access`, `fseeko`.

- **Audio and USB** (u-feckless-0021, `src/box_audio.c`), as Lothesome
  uses them:
  - `audio_out_init` (the bus's and the first expander's numbers),
    `audio_out_set_volume`, `audio_out_headphones`,
    `audio_out_capture_begin`, `_read`, `_end`, `audio_out_monitor_write`.
  - `usbhost_init` (the second expander's number), `usbhost_start`.
  - `uac_init`, `uac_present`, `uac_stream_start`, `uac_stream_stop`,
    `uac_write`, `uac_product`, `uac_mic_announced`, `uac_mic_open_bits`,
    `uac_mic_read`, `uac_mic_gone`, `uac_mic_close`, `uac_mic_product`.

  Every buffer is checked to lie in the box for its whole length: a
  capture read for `max_frames * 2` int32 (what `audio_out.h` asks of the
  caller), a microphone read for `max_frames` times the channels the open
  reported, a monitor write for `frames * 2` int16. A read or write waits
  at most 2 s. The product strings are copied into the box, 127
  characters at most. `feckless_box_calls_end()` closes a microphone the
  app left open and stops its stream and capture.

- **Playback, the battery, the clock, more gfx** (u-feckless-0046), as
  Defeatist's player uses them:
  - `audio_out_write` (at most 64 KB a call, the buffer read in place),
    `audio_out_set_format`, `_rate_supported`, `_rate`, `_set_mute`,
    `_muted`, `_set_idle`, `_route`, `_route_name` (copied into the box);
  - `battery_init` (the bus's number), `_start`, `_pct`, `_mv`,
    `_charging`, `_external`, `_trace_arm` (the reason copied);
  - `rtc8130_init` (the bus's number), `_read`, `_write`, `_write_forward`;
  - `polyrsp_open`, `_close`, `_reset`, `_max_out`, `_process`: a number
    for each resampler (four at most), run on the host in M-mode, the
    output checked to hold what the input can make;
  - `usbhost_running`, `usbhost_vbus_on`;
  - `gfx_set_filter`, `gfx_filter`, `gfx_blit_err`, `gfx_set_rotation`,
    `gfx_fill_poly` (16 points, copied), `gfx_fill_star`,
    `gfx_draw_spinner`, `gfx_time_text`, `gfx_time_text_w`,
    `gfx_draw_time_text`, `gfx_draw_text_tail`, `gfx_draw_text_clipped`,
    `gfx_text_rtl`, `gfx_para_rows`; `lcd_backlight_set_counts`,
    `touch_swallow`.

  `feckless_box_calls_end()` closes the app's resamplers and, if it played
  anything, puts the amplifier back to idle.

- **NVS** (u-feckless-0053, `src/box_nvs.c`): feckless_nvs_handler's
  `nvsh_get_`/`nvsh_set_` u8, u32, i32, str and blob, `nvsh_erase_key`,
  `nvsh_erase_ns`, `nvsh_ready` and `feckless_abi_nvs_v1`, only in the namespaces the launcher
  named with `feckless_box_calls_nvs()` before the load (four at most).
  Any other reads as empty and refuses a change with ESP_ERR_NOT_ALLOWED;
  named none, the app has no NVS. Strings and blobs (8 KB at most) are
  copied through the host's heap. Unlocked. The names are forgotten at
  `feckless_box_calls_end()`.

- **esp_audio_codec's simple decoder** (u-feckless-0052, `src/box_codec.c`),
  where the launcher links the codec: `esp_audio_dec_register_default`,
  `esp_audio_simple_dec_register_default` (once each, ALREADY_EXIST taken
  as success), `esp_audio_simple_dec_open`, `_process`, `_get_info`,
  `_close`, and `feckless_abi_audio_v1`. Four decoders at once, a number
  each; the decode runs on the host in M-mode, unlocked, one call per
  decoder at a time. The configuration is copied in (ALAC's cookie too);
  Vorbis's raw headers are refused. Without the codec in the launcher, or
  with `FECKLESS_BOX_NO_CODEC`, the table is empty.

- **FreeRTOS** (u-feckless-0045, `src/box_rtos.c`), as Defeatist uses it:
  - mutexes, recursive mutexes, binary and counting semaphores and queues
    (`xQueueGenericCreate`, `xQueueCreateMutex`,
    `xQueueCreateCountingSemaphore`, `vQueueDelete`, `xQueueGenericSend`,
    `xQueueSemaphoreTake`, `xQueueReceive`, `xQueuePeek`,
    `uxQueueMessagesWaiting`, `xQueueGenericReset`,
    `xQueueTakeMutexRecursive`, `xQueueGiveMutexRecursive`);
  - stream and message buffers (`xStreamBufferGenericCreateStatic`,
    `xStreamBufferGenericCreate`, `vStreamBufferDelete`, `_Send`,
    `_Receive`, `_BytesAvailable`, `_SpacesAvailable`, `_IsEmpty`,
    `_IsFull`, `_Reset`);
  - task notifications (`xTaskGenericNotify`, `ulTaskGenericNotifyTake`,
    `xTaskGenericNotifyWait`), index 0;
  - critical sections (`xPortEnterCriticalTimeout`, `vPortEnterCritical`,
    `vPortExitCriticalMultiCore`, `vPortExitCritical`), one recursive mutex
    for the app; `xPortInIsrContext` (never);
  - tasks: `xTaskCreateStaticPinnedToCore`, `xTaskGetTickCount`,
    `xTaskGetCurrentTaskHandle`, `pcTaskGetName`,
    `uxTaskGetStackHighWaterMark`, `xTaskGetStackStart`,
    `uxTaskPriorityGet`, `vTaskPrioritySet`, `eTaskGetState`,
    `vTaskSuspend` (the caller only).

  Every object is the host's, with a number in the box; a static create's
  buffers are not used. The calls that wait are unlocked and wait in 100 ms
  slices, giving up when the app stops. Notifications are a value and a
  state per thread kept here, not the host task's own. A thread made with
  `tskNO_AFFINITY` goes on the caller's core.

## Tests

    make -C components/feckless_box_calls/test

Every call through `feckless_umode`'s ext hook as a box makes it, against
recording stand-ins for the board; and storage as Spurious calls it, against
a stand-in FatFs and storage handler over real files, and the host's own
stdio and directories.
