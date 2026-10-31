# Host tests for the parts of this component that are decisions rather
# than drivers: the saved-network store (against a fake NVS) and when the
# cable counts as a network (header-only). Moved here from the player's
# texttest/ with the code they test; the rules are the ones they had
# there.
#
#   make -C test
#
# SPDX-License-Identifier: MIT

CC ?= cc

WARNCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
              -D_GNU_SOURCE -I. -Ifake -I../include
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -fsanitize=address,undefined -fno-sanitize-recover=all \
           -fno-omit-frame-pointer -I. -Ifake -I../include
LDFLAGS := -fsanitize=address,undefined -lm

all: run-wifistore run-netlink

wifistoretest: wifistoretest.c ../src/wifistore.c ../include/wifistore.h fake/wifistore_host.h
	$(CC) $(CFLAGS) -DWIFISTORE_HOST -o $@ wifistoretest.c ../src/wifistore.c $(LDFLAGS)

run-wifistore: wifistoretest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./wifistoretest

netlinktest: netlinktest.c ../include/netlink.h ../include/ethcfg.h
	$(CC) $(WARNCFLAGS) -c netlinktest.c -o /dev/null
	$(CC) $(CFLAGS) -o $@ netlinktest.c $(LDFLAGS)

run-netlink: netlinktest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./netlinktest

clean:
	rm -f wifistoretest netlinktest

.PHONY: all run-wifistore run-netlink clean
