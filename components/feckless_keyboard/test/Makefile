# Host test for feckless_keyboard:
#   kbdtest   the stagger, every pixel's tap against the key drawn over
#             it, shift, the symbol page, and every printable character
#             having a key (8011; 8010's checked the lift against Spurious)
#   preview   render.c: the four states drawn by the real gfx.c, as PPMs;
#             not part of `all`, for looking at
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

GFXSRC := $(GFX)/test/shim.c $(GFX)/src/gfx.c $(GFX)/fonts/ark12/ark12.c \
          $(GFX)/fonts/arabixel/arabixel_tab.c $(GFX)/fonts/arabixel/arabixel_shape.c

preview: render.c ../src/feckless_keyboard.c ../include/feckless_keyboard.h
	$(CC) -std=c11 -O1 -D_GNU_SOURCE -Wno-unused-parameter $(INC) -I$(GFX)/fonts/arabixel \
	    -o render render.c ../src/feckless_keyboard.c $(GFXSRC) -lm
	./render

clean:
	rm -f kbdtest render kb_*.ppm

.PHONY: all run warn preview clean
