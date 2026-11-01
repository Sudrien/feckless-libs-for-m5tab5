# Host test for feckless_libversions:
#   libvtest  libv_draw()'s rows, columns, paging and "and N more", against
#             recorded gfx_draw_text calls
#   gen       cmake/libversions.cmake in script mode against fixture/, a
#             lock and a vendored component, checked by gencheck.cmake
#
#   make -C components/feckless_libversions/test
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

all: run warn gen

libvtest: libvtest.c ../src/feckless_libversions.c ../include/feckless_libversions.h
	$(CC) $(CFLAGS) -o $@ libvtest.c ../src/feckless_libversions.c $(LDFLAGS)

run: libvtest
	./libvtest

warn:
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/feckless_libversions.c

gen:
	rm -rf out && mkdir out
	cd out && cmake -DROOT=../fixture -DLV_NAME=Fixture -P ../../cmake/libversions.cmake
	cmake -DHEADER=out/libversions/libversions.h -P gencheck.cmake
	$(CC) $(WARNCFLAGS) -Iout/libversions -x c -c -o /dev/null - < /dev/null -include out/libversions/libversions.h

clean:
	rm -rf libvtest out

.PHONY: all run warn gen clean
