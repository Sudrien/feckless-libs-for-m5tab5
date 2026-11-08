# printf, vendored

`printf.c` and `printf.h` are eyalroz/printf at tag v6.4.0
(https://github.com/eyalroz/printf, commit 92c7359), unchanged; `LICENSE` is
its MIT licence. `src/box_printf.c` includes `printf.c` with the standard
names aliased (`PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD`).

Why not picolibc's: the toolchain's `libc.a` is not position-independent,
and its `vfprintf` has absolute references to its tables
(`R_RISCV_HI20 ... can not be used when making a shared object`), so it cannot
be linked into an app's ELF. This one, compiled with the app's `-fPIC`, can.
