# Host test for nvsh_migrate(): nvs_handler.c as it is, against an
# in-memory NVS (fake/).
#
#   make -C components/feckless_nvs_handler/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer -Ifake -I../include
LDFLAGS := -fsanitize=address,undefined

all: run

nvstest: nvstest.c ../src/nvs_handler.c ../include/nvs_handler.h fake/fake_nvs.c fake/nvs.h
	$(CC) $(CFLAGS) -o $@ nvstest.c ../src/nvs_handler.c fake/fake_nvs.c $(LDFLAGS)

run: nvstest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./nvstest

clean:
	rm -f nvstest
.PHONY: all run clean
