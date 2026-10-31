# Host test for feckless_image.h and image_ico.c, as they are.
#
#   make -C components/feckless_image_handler/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer -I../include
LDFLAGS := -fsanitize=address,undefined

all: run

imagetest: imagetest.c ../src/image_ico.c ../include/feckless_image.h
	$(CC) $(CFLAGS) -o $@ imagetest.c ../src/image_ico.c $(LDFLAGS)

run: imagetest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./imagetest

clean:
	rm -f imagetest
.PHONY: all run clean
