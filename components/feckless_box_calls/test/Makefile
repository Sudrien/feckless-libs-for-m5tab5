# Host test for feckless_box_calls: box_calls.c as it is, through
# feckless_umode's ext hook, against recording stand-ins for the board.
#
#   make -C components/feckless_box_calls/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
U := ../../feckless_umode
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
          -Ifake -I../include -I$(U)/include
LDFLAGS := -fsanitize=address,undefined -lm
SRC := box_calls_test.c fake_board.c fake_storage.c fake_audio.c ../src/box_calls.c ../src/box_storage.c ../src/box_audio.c $(U)/src/umode_calls.c $(U)/src/umode_box.c $(U)/src/umode_run.c

all: run

box_calls_test: $(SRC) $(wildcard fake/*.h fake/freertos/*.h) ../src/box_calls_priv.h ../include/feckless_box_calls.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

SSRC := box_storage_test.c fake_board.c fake_storage.c fake_audio.c ../src/box_calls.c ../src/box_storage.c ../src/box_audio.c $(U)/src/umode_calls.c $(U)/src/umode_box.c $(U)/src/umode_run.c

box_storage_test: $(SSRC) $(wildcard fake/*.h fake/freertos/*.h) ../src/box_calls_priv.h ../include/feckless_box_calls.h
	$(CC) $(CFLAGS) -o $@ $(SSRC) $(LDFLAGS)

run: box_calls_test box_storage_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./box_calls_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./box_storage_test

clean:
	rm -f box_calls_test box_storage_test
.PHONY: all run clean
