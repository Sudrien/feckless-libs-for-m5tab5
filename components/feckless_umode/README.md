# feckless_umode

RISC-V user mode for the ESP32-P4 under ESP-IDF's FreeRTOS, as an ESP-IDF
component: a context that runs in U-mode until it traps, and the PMP grants
that say what it may touch. Nothing in it dispatches a syscall yet; that is
the next patch of the `umode` branch.

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
  cores' monitors, the caller must un-route it on every core before the first
  window, and ESP-IDF then no longer panics on an M-mode stack overflow
  (`Documentation/hard-won-lessons.md` §3.5 of the sample says why). That is
  not done here yet.

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
