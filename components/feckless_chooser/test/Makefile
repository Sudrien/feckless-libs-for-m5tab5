# Host test for feckless_chooser (9012):
#   choosertest  keys, taps, a volume going, the app's rows, and what is
#                drawn, over the real storage_list.c on a folder in /tmp,
#                under ASan and UBSan
#   warn         the component as the firmware compiles it: -O2, every
#                warning an error
#   preview      render.c: drawn by the real gfx.c, as PPMs; not part of
#                `all`, for looking at
#
#   make -C components/feckless_chooser/test
#
# gfx.h and storage_list.h are the real ones; feckless_graphics_handler's
# test/fake has the ESP-IDF headers gfx.h includes, and
# feckless_storage_handler's test/fake storage.h's.
#
# SPDX-License-Identifier: MIT

CC ?= cc
GFX := ../../feckless_graphics_handler
STO := ../../feckless_storage_handler
INC := -I../include -I$(GFX)/include -I$(GFX)/fonts/ark12 -I$(GFX)/test/fake -I$(GFX)/test \
       -I$(STO)/include -I$(STO)/test/fake
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror -DSTORAGE_LIST_HOST \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer $(INC)
LDFLAGS := -fsanitize=address,undefined
WARNCFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror $(INC)

SRC := ../src/feckless_chooser.c $(STO)/src/storage_list.c
HDR := ../include/feckless_chooser.h $(STO)/include/storage_list.h

all: run warn

choosertest: choosertest.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ choosertest.c $(SRC) $(LDFLAGS)

run: choosertest
	./choosertest

warn:
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/feckless_chooser.c

GFXSRC := $(GFX)/test/shim.c $(GFX)/src/gfx.c $(GFX)/fonts/ark12/ark12.c \
          $(GFX)/fonts/arabixel/arabixel_tab.c $(GFX)/fonts/arabixel/arabixel_shape.c

preview: render.c $(SRC) $(HDR)
	$(CC) -std=gnu11 -O1 -D_GNU_SOURCE -DSTORAGE_LIST_HOST -Wno-unused-parameter $(INC) \
	    -I$(GFX)/fonts/arabixel -o render render.c $(SRC) $(GFXSRC) -lm
	./render jinxed && ./render avoidant

clean:
	rm -f choosertest render chooser_*.ppm

.PHONY: all run warn preview clean
