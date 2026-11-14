# Host tests for feckless_box_libc: strto* and the vendored printf against
# the host's libc, the maths (u-feckless-0020) against its libm, and the
# double arithmetic (u-feckless-0036) against its FPU, the 64-bit
# division (u-feckless-0038) against its own, and the double libm
# (u-feckless-0041) against the toolchain's.
#
#   make -C components/feckless_box_libc/test
#
# The toolchain-side checks (the subsets extracted, an app linked into a
# shared object, abicheck and boxcheck) need riscv32-esp-elf and are recorded
# in ARCHITECTURE.md (u-feckless-0005).
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

all: run

strto_test: strto_test.c ../src/box_strto.c
	$(CC) $(CFLAGS) -D'BOX_NAME(n)=box_##n' -o $@ strto_test.c ../src/box_strto.c $(LDFLAGS)

printf_test: printf_test.c ../vendor/printf/printf.c
	$(CC) $(CFLAGS) -w -I../vendor -o $@ printf_test.c ../vendor/printf/printf.c $(LDFLAGS)

# u-feckless-0020. -ffp-contract=off: the target's float has no fused
# multiply-add unless asked, and the host's compiler must not add one.
math_test: math_test.c ../src/box_math.c
	$(CC) $(CFLAGS) -ffp-contract=off -fno-builtin -D'BOX_NAME(n)=box_##n' -o $@ math_test.c ../src/box_math.c -lm $(LDFLAGS)

# u-feckless-0027
fpconv_test: fpconv_test.c ../src/box_fpconv.c
	$(CC) $(CFLAGS) -D'BOX_NAME(n)=box_##n' -o $@ fpconv_test.c ../src/box_fpconv.c $(LDFLAGS)

# u-feckless-0036. Against the host's doubles, bit for bit, so the host must
# round each operation once: no fused multiply-add, and no builtin that
# would fold the test's own arithmetic. -O2 and no sanitizers: it is 190
# million checks, and the file under test is integer arithmetic on values,
# with no pointer in it.
softdouble_test: softdouble_test.c ../src/box_softdouble.c
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffp-contract=off -fno-builtin \
	      -D'BOX_NAME(n)=box_##n' -D'BOX_DIV_FIX()=box_div_fix()' \
	      -o $@ softdouble_test.c ../src/box_softdouble.c

# u-feckless-0038. Against the host's 64-bit division; 237 million checks,
# so -O2 like softdouble_test, and the same reason sanitizers add little.
div64_test: div64_test.c ../src/box_div64.c
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -D'BOX_NAME(n)=box_##n' \
	      -o $@ div64_test.c ../src/box_div64.c

# u-feckless-0041. newlib's own sources, built for the host, against the
# toolchain's libm.a: see newlib_math_test.c for why that can be done here.
NEWLIB_MATH := ../newlib_math
NEWLIB_MATH_SRCS := $(wildcard $(NEWLIB_MATH)/math/*.c $(NEWLIB_MATH)/common/*.c)
newlib_math_test: newlib_math_test.c $(NEWLIB_MATH_SRCS) newlib_shim/machine/ieeefp.h
	$(CC) -std=gnu11 -O2 -w -ffp-contract=off -fno-builtin -Inewlib_shim -I$(NEWLIB_MATH)/common \
	      -o $@ newlib_math_test.c $(NEWLIB_MATH_SRCS)

run: strto_test printf_test math_test fpconv_test softdouble_test div64_test newlib_math_test
	./strto_test
	./printf_test
	./math_test
	./fpconv_test
	./softdouble_test
	./div64_test
	./newlib_math_test

clean:
	rm -f strto_test printf_test math_test fpconv_test softdouble_test div64_test newlib_math_test
.PHONY: all run clean
