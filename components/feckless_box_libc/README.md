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

- **Single-precision maths** (u-feckless-0020, `src/box_math.c`, this
  component's own): `sinf`, `cosf`, `sincosf`, `tanf`, `atanf`, `atan2f`,
  `asinf`, `acosf`, `tanhf`, `expf`, `exp2f`, `exp10f`, `logf`, `log2f`,
  `log10f`, `powf`, `sqrtf`, `fmodf`, `floorf`, `ceilf`, `truncf`, `roundf`,
  `lroundf`, `lrintf`, `fabsf`, `copysignf`, `fmaxf`, `fminf`, and (u-feckless-0025)
  `ldexpf`, `rintf`, `hypotf`, `cbrtf`. picolibc's
  are in its `libc.a` but not position-independent. A few ulp, not
  correctly rounded; no `errno`. The ABI's double-precision names (`sqrt`,
  `sin`, `pow` and the rest) are host calls in `feckless_box_calls`
  (u-feckless-0025).
- **`heap_caps_malloc`, `_calloc`, `_realloc`, `_free`** (u-feckless-0020,
  `src/box_heap.c`): the box's own heap, whatever the caps say.

- **`vTaskDelete(NULL)`** (u-feckless-0023, `src/box_task.c`): ends the
  calling thread. `errno` is per thread from 0023: the word at `tp`, which
  feckless_umode points at a block of each thread's own.

- **libgcc's float conversions** (u-feckless-0027, `src/box_fpconv.c`):
  `__extendsfdf2`, `__floatdisf`, `__floatundisf`, `__fixsfdi`,
  `__fixunssfdi`, written on the bits (their float travels in `fa0`, which
  no host call carries), and `__issignalingf`.

- **Double arithmetic** (u-feckless-0036, `src/box_softdouble.c`):
  `__adddf3`, `__subdf3`, `__muldf3`, `__divdf3`, the conversions from an
  integer (`__floatsidf`, `__floatunsidf`, `__floatdidf`, `__floatundidf`)
  and the bit counts (`__clzsi2`, `__ctzsi2`, `__clzdi2`). The P4's FPU is
  single precision, so each double operation is a call; these were host
  calls, a trap an operation, and are now in the app. IEEE 754, round to
  nearest even, the canonical NaN. Division is by a reciprocal and an
  exact remainder (u-feckless-0039).

- **Double maths** (u-feckless-0041, `newlib_math/`): `sqrt`, `sin`,
  `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `asinh`, `exp`, `log`,
  `log10`, `floor`, `ceil`, `round`, `fabs`, `atan2`, `pow`, `fmod`,
  `ldexp`, `lround`, and `copysign`, `expm1`, `log1p`, `scalbn`, `finite`,
  `nan`. Espressif's newlib sources, unchanged, compiled into the app: the
  same code the launcher answered them with as host calls, now without the
  trap, and the same bits.

- **64-bit integer division** (u-feckless-0038, `src/box_div64.c`):
  `__udivdi3`, `__umoddi3`, `__divdi3`, `__moddi3`, which is `/` and `%` on
  an `int64_t` or `uint64_t`. Also host calls until then. A divisor under
  65536 (microseconds by 1000) takes four hardware divides.

Not here, so not usable in a box yet: `sscanf`, `strtod`, `strtof`, `atof`,
`strftime`, `rand` (picolibc's are not position-independent).

## Tests

    make -C components/feckless_box_libc/test

`strto*` against the host's libc (800 000 comparisons), the vendored printf
against the host's `snprintf`, the float conversions bit for bit against
the host's (9 million), the maths against the host's libm (the worst
error of each function over a sweep, and the special values), the double
arithmetic bit for bit against the host's FPU (190 million, about a
quarter of a minute), the 64-bit division against the host's (237 million,
a few seconds), the double maths against the toolchain's libm.a (4.4
million calls, by hashes taken from it under qemu-riscv32).
