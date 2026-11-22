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
SRC := box_calls_test.c fake_net.c fake_http.c fake_httpd.c fake_board.c fake_storage.c fake_audio.c fake_rtos.c fake_drivers.c fake_codec.c fake_nvs.c fake_camera.c fake_periph.c ../src/box_calls.c ../src/box_storage.c ../src/box_audio.c ../src/box_rtos.c ../src/box_codec.c ../src/box_nvs.c ../src/box_camera.c ../src/box_drivers.c ../src/box_image.c ../src/box_net.c ../src/box_http.c ../src/box_upcall.c ../src/box_netif.c ../src/box_httpd.c ../src/box_crypto.c $(U)/src/umode_calls.c $(U)/src/umode_box.c $(U)/src/umode_run.c

all: run

box_calls_test: $(SRC) $(wildcard fake/*.h fake/freertos/*.h fake/driver/*.h fake/lwip/*.h) ../src/box_pins.h ../src/box_calls_priv.h ../include/feckless_box_calls.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

SSRC := box_storage_test.c fake_net.c fake_http.c fake_httpd.c fake_board.c fake_storage.c fake_audio.c fake_rtos.c fake_drivers.c fake_codec.c fake_nvs.c fake_camera.c fake_periph.c ../src/box_calls.c ../src/box_storage.c ../src/box_audio.c ../src/box_rtos.c ../src/box_codec.c ../src/box_nvs.c ../src/box_camera.c ../src/box_drivers.c ../src/box_image.c ../src/box_net.c ../src/box_http.c ../src/box_upcall.c ../src/box_netif.c ../src/box_httpd.c ../src/box_crypto.c $(U)/src/umode_calls.c $(U)/src/umode_box.c $(U)/src/umode_run.c

box_storage_test: $(SSRC) $(wildcard fake/*.h fake/freertos/*.h fake/driver/*.h fake/lwip/*.h) ../src/box_pins.h ../src/box_calls_priv.h ../include/feckless_box_calls.h
	$(CC) $(CFLAGS) -o $@ $(SSRC) $(LDFLAGS)

# u-feckless-0024: box_storage.c without fast seek, as an app whose sdkconfig
# lacks CONFIG_FATFS_USE_FASTSEEK builds it -- FIL then has no cltbl.
nofastseek:
	$(CC) $(CFLAGS) -DFF_USE_FASTSEEK=0 -fsyntax-only ../src/box_storage.c

# u-feckless-0052: box_codec.c where the launcher has no codec to answer with
nocodec:
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_CODEC -fsyntax-only ../src/box_codec.c

# u-feckless-0072: the card's lease against the app's lock, with real threads
LSRC := leaselock_test.c $(filter-out box_storage_test.c,$(SSRC))
leaselock_test: $(LSRC) $(wildcard fake/*.h fake/freertos/*.h fake/driver/*.h fake/lwip/*.h) ../src/box_calls_priv.h
	$(CC) $(CFLAGS) -DFAKE_REAL_LOCKS -o $@ $(LSRC) $(LDFLAGS) -lpthread

run-leaselock: leaselock_test
	timeout 60 ./leaselock_test

# u-feckless-0075: callbacks into the app, with real threads and semaphores
USRC := upcall_test.c $(filter-out box_storage_test.c,$(SSRC))
upcall_test: $(USRC) $(wildcard fake/*.h fake/freertos/*.h fake/driver/*.h fake/lwip/*.h) ../src/box_calls_priv.h
	$(CC) $(CFLAGS) -DFAKE_REAL_LOCKS -DFAKE_REAL_SEMS -o $@ $(USRC) $(LDFLAGS) -lpthread

run-upcall: upcall_test
	timeout 60 ./upcall_test

# u-feckless-0077: the HTTP server, end to end, with real threads
HSRC := httpd_test.c $(filter-out box_storage_test.c,$(SSRC))
httpd_test: $(HSRC) fake_httpd.h $(wildcard fake/*.h fake/freertos/*.h fake/driver/*.h fake/lwip/*.h) ../src/box_calls_priv.h
	$(CC) $(CFLAGS) -DFAKE_REAL_LOCKS -DFAKE_REAL_SEMS -o $@ $(HSRC) $(LDFLAGS) -lpthread

run-httpd: httpd_test
	timeout 60 ./httpd_test

# u-feckless-0071: box_net.c where the launcher has no network to answer with
nonet:
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_NET -fsyntax-only ../src/box_net.c
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_NET -fsyntax-only ../src/box_http.c
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_NET -fsyntax-only ../src/box_netif.c
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_NET -fsyntax-only ../src/box_httpd.c
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_CRYPTO -fsyntax-only ../src/box_crypto.c

# u-feckless-0059: box_camera.c where the launcher has no camera to answer with
nocamera:
	$(CC) $(CFLAGS) -DFECKLESS_BOX_NO_CAMERA -fsyntax-only ../src/box_camera.c

run: nofastseek nocodec nocamera nonet run-leaselock run-upcall run-httpd box_calls_test box_storage_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./box_calls_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./box_storage_test

clean:
	rm -f box_calls_test box_storage_test leaselock_test upcall_test httpd_test
.PHONY: all run clean nofastseek nocodec nocamera nonet run-leaselock run-upcall run-httpd
