# Host tests for feckless_box_libc: strto* and the vendored printf against
# the host's libc.
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

run: strto_test printf_test
	./strto_test
	./printf_test

clean:
	rm -f strto_test printf_test
.PHONY: all run clean
