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
- **The FPU and the PIE unit.** ESP-IDF turns each on for a task at its
  first use, from the illegal-instruction trap, which in user mode is the
  program's own fault. The FPU is switched on before every window
  (u-feckless-0028). The PIE unit is switched on when a program first
  executes a PIE instruction, its registers zeroed, and from then on before
  every window of that program (u-feckless-0034). PIE registers are not
  kept across a host call.

## An app in a box (u-feckless-0004)

`feckless_umode_load()` puts an app's ELF in a **box**: one block of PSRAM
that holds the app's code, data, heap, stack and the stubs it calls the host
with, and is all the PMP grants. The component's link wraps `esp_elf_malloc`
and `esp_elf_free`, so while a box is being loaded esp_elf allocates in it;
otherwise they pass through.

- **Imports** resolve to the box's stubs: `mv t0, a7; addi a7, x0, 16+i;
  ecall; ret` (a7 is an eighth argument; `feckless_umode_arg()` finds it in t0).
  The embedder gives the table (`feckless_umode_call_t`: a name and a
  handler, or a name and data to copy in, for version symbols). An import
  not in it fails the load by name, as ABI v1 does.
- **The heap** (`feckless_umode_heap_calls`) allocates in the box. Its
  bookkeeping is the host's, never in the box, and a free of anything but the
  start of an allocation is refused.
- **Constructors and the entry** run in U-mode, each in a window, returning
  through an exit stub. `esp_elf_request()` is not used (it would call them
  in M-mode), and `fini` is cleared before `esp_elf_deinit()` for the same
  reason.

## Limits, as of this patch

- **Windows are interruptible** (u-feckless-0016). An interrupt in user mode
  ends the window like any trap; IDF's handler takes it as the host resumes,
  and the window is re-entered, so a long computation between calls no longer
  holds its core's interrupts -- the tick, the panel's frame DMA -- off. An
  edge-triggered line is set pending again on the way out, so none is lost.
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
