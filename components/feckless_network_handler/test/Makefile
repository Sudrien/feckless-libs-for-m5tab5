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

all: run-wifistore run-netlink run-redirect run-fetch

redirecttest: redirecttest.c ../src/feckless_redirect.c ../include/feckless_redirect.h
	$(CC) $(WARNCFLAGS) -c redirecttest.c -o /dev/null
	$(CC) $(WARNCFLAGS) -c ../src/feckless_redirect.c -o /dev/null
	$(CC) $(CFLAGS) -o $@ redirecttest.c ../src/feckless_redirect.c $(LDFLAGS)

run-redirect: redirecttest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./redirecttest

# 8004: feckless_cert.c against the real Mbed TLS, then OpenSSL on what it
# made. Not part of `all`: it needs a built Mbed TLS tree of each major,
#   MBED36=<mbedtls-3.6 tree, `make lib` done>
#   MBED40=<mbedtls-4.0 tree, `cmake --build build` done in build/>
# and `make run-cert` runs whichever of them is set.
MBED36 ?=
MBED40 ?=
CERTFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -I../include
CERTOUT   := $(or $(TMPDIR),/tmp)/feckless_certtest

certtest36: certtest.c ../src/feckless_cert.c ../include/feckless_cert.h
	$(CC) $(CERTFLAGS) -I$(MBED36)/include -o $@ certtest.c ../src/feckless_cert.c \
	    $(MBED36)/library/libmbedx509.a $(MBED36)/library/libmbedcrypto.a

certtest40: certtest.c ../src/feckless_cert.c ../include/feckless_cert.h
	$(CC) $(CERTFLAGS) -I$(MBED40)/include -I$(MBED40)/tf-psa-crypto/include \
	    -I$(MBED40)/tf-psa-crypto/drivers/builtin/include -I$(MBED40)/build/tf-psa-crypto/include \
	    -o $@ certtest.c ../src/feckless_cert.c \
	    $(MBED40)/build/library/libmbedx509.a $(MBED40)/build/library/libtfpsacrypto.a

run-cert36: certtest36
	rm -rf $(CERTOUT)36 && mkdir -p $(CERTOUT)36
	./certtest36 $(CERTOUT)36 && ./certcheck.sh $(CERTOUT)36

run-cert40: certtest40
	rm -rf $(CERTOUT)40 && mkdir -p $(CERTOUT)40
	./certtest40 $(CERTOUT)40 && ./certcheck.sh $(CERTOUT)40

# The same file without the X.509 writer is two stubs, and links with no
# Mbed TLS at all. Needs the headers only.
certnowriter36: certnowriter.c ../src/feckless_cert.c fake/nowriter36.h
	$(CC) $(CERTFLAGS) -I$(MBED36)/include -DMBEDTLS_USER_CONFIG_FILE='"fake/nowriter36.h"' -I. \
	    -o $@ certnowriter.c ../src/feckless_cert.c

certnowriter40: certnowriter.c ../src/feckless_cert.c fake/nowriter40.h
	$(CC) $(CERTFLAGS) -I$(MBED40)/include -I$(MBED40)/tf-psa-crypto/include \
	    -I$(MBED40)/tf-psa-crypto/drivers/builtin/include -I$(MBED40)/build/tf-psa-crypto/include \
	    -DMBEDTLS_USER_CONFIG_FILE='"fake/nowriter40.h"' -I. \
	    -o $@ certnowriter.c ../src/feckless_cert.c

run-certnowriter36: certnowriter36
	./certnowriter36

run-certnowriter40: certnowriter40
	./certnowriter40

run-cert: $(if $(MBED36),run-cert36 run-certnowriter36) $(if $(MBED40),run-cert40 run-certnowriter40)

# 9003: feckless_fetch.c, its tasks on pthreads and the network an
# in-memory server (fake/fetchfake.c). Also under ThreadSanitizer with
# `make run-fetch-tsan`, which is not part of `all`.
FETCHSRC := fetchtest.c ../src/feckless_fetch.c fake/fetchfake.c
FETCHDEP := $(FETCHSRC) ../include/feckless_fetch.h fake/fetchfake.h fake/esp_http_client.h

fetchtest: $(FETCHDEP)
	$(CC) $(WARNCFLAGS) -c ../src/feckless_fetch.c -o /dev/null
	$(CC) $(WARNCFLAGS) -c fake/fetchfake.c -o /dev/null
	$(CC) $(WARNCFLAGS) -c fetchtest.c -o /dev/null
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $(FETCHSRC) $(LDFLAGS) -lpthread

run-fetch: fetchtest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./fetchtest

fetchtest-tsan: $(FETCHDEP)
	$(CC) -std=c11 -g -O1 -D_GNU_SOURCE -fsanitize=thread -I. -Ifake -I../include \
	    -o $@ $(FETCHSRC) -lpthread

run-fetch-tsan: fetchtest-tsan
	./fetchtest-tsan

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
	rm -f wifistoretest netlinktest redirecttest fetchtest fetchtest-tsan certtest36 certtest40 certnowriter36 certnowriter40

.PHONY: all run-wifistore run-netlink run-redirect run-fetch run-fetch-tsan run-cert run-cert36 run-cert40 run-certnowriter36 run-certnowriter40 clean
