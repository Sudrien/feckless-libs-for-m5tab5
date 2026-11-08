# feckless_box_libc

The part of libc an app built for a box links into its own ELF
(the `umode` branch, u-feckless-0005). A boxed app runs in RISC-V U-mode in
one block of memory (`feckless_umode`); everything it imports is an `ecall`
to the launcher. Pure functions -- string and memory, `qsort` with the app's
own comparator, ctype, `strtol`, `snprintf` -- have no business crossing
that line, and `qsort` must not: the launcher would call the comparator in
M-mode. So they are in the app.

## Using it

```cmake
# CMakeLists.txt
project(myapp)
feckless_abi_box_elf(myapp)     # in place of feckless_abi_elf(myapp)
```

```c
#include "feckless_abi.h"
#include "feckless_box.h"

void app_main(void)
{
    feckless_abi_require();
    feckless_abi_require_box();
    ...
}
```

main `REQUIRES feckless_box_libc`, and the app's manifest names it at the
same tag as the other feckless components.

## What is in the app

`inapp.txt` is the list, and the build fails a boxed ELF that imports any of
it (`cmake/boxcheck.cmake`):

- **From the toolchain's picolibc `libc.a`**: the members that link into a
  shared object (`picolibc_names.txt`), copied out of `libc.a` by
  `cmake/archive_subset.cmake` -- not `libc.a` whole, which would also bring
  in its own `malloc`, locks and `abort`.
- **From `libgcc.a`**: the helpers that link the same way
  (`libgcc_names.txt`); the rest (`__adddf3`, `__divdi3` and others that reach
  `__clz_tab`) take and return values only, and stay imports for the host.
- **This component's own**: `strtol` and its family, `atoi`, `atol` and
  `errno` (picolibc's keep `errno` thread-local, which a shared object cannot
  have), and `snprintf`, `vsnprintf`, `sprintf`, `printf`, `vprintf` from
  eyalroz/printf v6.4.0 (`vendor/printf`, MIT; picolibc's has absolute
  references to its tables). `printf` writes a character at a time through
  the host's `putchar`.
- **The log calls** `esp_log`, `esp_log_va`, `esp_log_write`,
  `esp_log_writev` (u-feckless-0006): the line is formatted here, so the
  host never walks a format whose `%s` would be the app's pointer, and is
  handed to the host's `feckless_box_log` as a tag and a message.

Not here, so not usable in a box yet: `sscanf`, `strtod`, `strtof`, `atof`,
`strftime`, `rand` (picolibc's are not position-independent).

## Tests

    make -C components/feckless_box_libc/test

`strto*` against the host's libc (800 000 comparisons), the vendored printf
against the host's `snprintf`.
