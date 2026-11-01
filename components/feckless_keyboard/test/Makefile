# Host test for feckless_keyboard (8010):
#   kbdtest   feckless_keyboard.c against Spurious's code it was lifted
#             from: the same gfx calls, and the same text after a tap on
#             every pixel of the band
#
#   make -C components/feckless_keyboard/test
#
# gfx.h is the real one; feckless_graphics_handler's test/fake has the two
# ESP-IDF headers it includes. The gfx calls are recorded by kbdtest.c.
#
# SPDX-License-Identifier: MIT

CC ?= cc
GFX := ../../feckless_graphics_handler
INC := -I../include -I$(GFX)/include -I$(GFX)/fonts/ark12 -I$(GFX)/test/fake -I$(GFX)/test
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer $(INC)
LDFLAGS := -fsanitize=address,undefined
WARNCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror $(INC)

all: run warn

kbdtest: kbdtest.c ../src/feckless_keyboard.c ../include/feckless_keyboard.h
	$(CC) $(CFLAGS) -o $@ kbdtest.c ../src/feckless_keyboard.c $(LDFLAGS)

run: kbdtest
	./kbdtest

# The component as the firmware compiles it: -O2, every warning an error.
warn:
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/feckless_keyboard.c

clean:
	rm -f kbdtest

.PHONY: all run warn clean
