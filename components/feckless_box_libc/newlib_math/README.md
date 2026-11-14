# newlib_math

The double-precision maths a boxed app calls, compiled into the app
(u-feckless-0041): `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`,
`sinh`, `asinh`, `exp`, `log`, `log10`, `floor`, `ceil`, `round`, `fabs`,
`atan2`, `pow`, `fmod`, `ldexp`, `lround`, and what they call.

**These files are not this project's.** They are copied unchanged from
Espressif's newlib, the C library the ESP-IDF toolchain ships:

    https://github.com/espressif/newlib-esp32
    branch esp-4.5.0, commit 37f7f7d50cdf6e73bce48d74ec8ca6b62241e778
    newlib/libm/math/   -> math/
    newlib/libm/common/ -> common/

Each keeps its own notice: Sun Microsystems' fdlibm notice on most, Arm's
BSD-3-Clause on `math_err.c`, and `COPYING.NEWLIB` (copied here too) for
`s_nan.c`, which has none of its own and falls under its Red Hat notice.

**Why copied, not linked.** The toolchain's `libm.a` has these already,
but compiled without `-fPIC`: their constant tables are reached by
absolute address (`R_RISCV_HI20`), which a boxed app, a shared object
loaded anywhere, cannot have. The same was true of libgcc (0005, 0036).
So the box answered them as host calls (0025), a trap each.

**Why these files.** The toolchain's `libm.a` was built from them. Its
members for those 21 names are these 42 objects (`copysign` is the 43rd
source, which `atan2` and `asinh` reach). The P4 has no double FPU, so
newlib's `ieeefp.h` chooses the old fdlibm code for doubles
(`__OBSOLETE_MATH` 1), and the RISC-V overrides in `machine/riscv/` fall
back to these generic files for the same reason. Compiled here and run
under qemu-riscv32, each of the 21 gives the same bits as the toolchain's
own on 200,000 inputs, and sets `errno` the same way.

To update: copy the same files from a newer commit, say which in this
file, and run `make -C ../test newlib_math_test`.
