# feckless_umode

RISC-V user mode for the ESP32-P4 under ESP-IDF's FreeRTOS, as an ESP-IDF
component: a context that runs in U-mode until it traps, and the PMP grants
that say what it may touch. The host loop services a handful of syscalls and
hands the rest to the embedder.

It is the machinery of NevynUK's
[ESP32-P4-Protected-Mode-Sample](https://github.com/NevynUK/ESP32-P4-Protected-Mode-Sample)
(see `NOTICE`), made a component, with the PMP grants taken from a list the
caller gives instead of a fixed one.

## How it works

- **U-mode is a coroutine hosted by an ordinary task.** `feckless_umode_enter(ctx)`
  parks the caller's registers and trap vectors, masks interrupts, and `mret`s
  to U-mode at `ctx->pc`. Every way back (an `ecall`, a fault, an interrupt)
  returns through the same call with the user's registers in `ctx`; the
  return value is `mcause`. The trap vector does no dispatching, so whatever
  the caller does next, including blocking, is in task context.
- **The PMP is taken over.** U-mode gets nothing until a grant gives a range
  back. `feckless_umode_pmp_apply(grants, n)` writes the plan to every core
  and reads it back; a grant costs two of the sixteen entries. Entries are
  never locked, so they bind U-mode and leave M-mode alone.
- **ESP-IDF must not have locked the PMP first.** Set
  `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n` (and `rm sdkconfig`). The
  component stops the configure otherwise. The price is IDF's PMA entries,
  which fault on accesses to unmapped gaps: a debugging aid, not a property
  of real memory.
- **`CONFIG_ESP_SYSTEM_HW_STACK_GUARD`** is handed over to the user's stack
  for the window and back, so a user stack overflow is the user's fault and
  not a kernel panic. Because the guard's interrupt source is shared by both
  cores' monitors, `feckless_umode_guard_unroute()` takes it out of the routing
  on every core, once, before the first window, and ESP-IDF then no longer
  panics on an M-mode stack overflow (the sample's `hard-won-lessons.md` 3.5
  says why).
- **The host loop** (`feckless_umode_run()`, no ESP-IDF in it): enter the
  context; on return read the stack-guard verdict first, then re-enter after an
  interrupt, service an `ecall`, or end on anything else. The calls are NOP,
  WRITE, DELAY_MS, YIELD, EXIT and GETCORE (`feckless_umode_sys.h`, which also
  has the `ecall` stubs a U-mode program uses); numbers from 16 go to the
  window's `ext` function. A pointer from the program goes through
  `feckless_umode_range_ok()` first, and a rejected one is an error code in
  `a0`, not the end of the program. The window, which holds the saved
  registers, must not be in memory the program can write; `run` refuses one
  that overlaps its own range.
- **`feckless_umode_run_default()`** is that loop on FreeRTOS (vTaskDelay,
  taskYIELD, the core id, stdout).

## Limits, as of this patch

- **One interrupt-masked window.** Interrupts are masked for the whole of a
  window, so a long computation between traps delays the tick on its core.
- **The grants are the caller's.** Nothing here decides what an app is given.
- **Verified** only on ESP-IDF 5.5.4 and a chip revision v1.0, by the sample;
  this component has not been built with ESP-IDF or run on a board.

## Tests

    make -C components/feckless_umode/test            # the plan and the context helper
    make -C components/feckless_umode/test asm        # umode.S assembles, stack guard off and on
    make -C components/feckless_umode/test compare SAMPLE=<sample>/main

`compare` assembles the sample's `umode.S` and this one and checks that
`feckless_umode_enter`, the trap entry, the CSR reads and `pmp_program` are
the same instructions. `asm` and `compare` use clang with the riscv32 target;
the stack guard's macros are stand-ins there, so what they check is the
syntax and the arithmetic around them.
