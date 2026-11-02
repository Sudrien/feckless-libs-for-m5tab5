# Host test for feckless_mail:
#   mailtest  mailcfg's checks and NVS round trip (fake_nvs.c), and
#             mailform driven by keys and taps through the real gfx.c and
#             feckless_keyboard, portrait with the on-screen keyboard and
#             landscape without, every pixel of each band tapped
#   preview   the same, writing mail_*.ppm to look at
#   smtptest  mailsmtp.c, the SMTP conversation, against a scripted server,
#             each script whole and a byte at a time (9001)
#   warn      the sources as the firmware compiles them
#
#   make -C components/feckless_mail/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
GFX := ../../feckless_graphics_handler
INC := -I../include -I../../feckless_keyboard/include -I../../feckless_nvs_handler/include \
       -I$(GFX)/include -I$(GFX)/fonts/ark12 -I$(GFX)/fonts/arabixel -I$(GFX)/test/fake -I$(GFX)/test
GFXSRC := $(GFX)/test/shim.c $(GFX)/src/gfx.c $(GFX)/fonts/ark12/ark12.c \
          $(GFX)/fonts/arabixel/arabixel_tab.c $(GFX)/fonts/arabixel/arabixel_shape.c
SMTPSRC := smtptest.c ../src/mailsmtp.c ../src/mailcfg.c fake_nvs.c $(GFX)/test/shim.c
SRC := mailtest.c fake_nvs.c ../src/mailcfg.c ../src/mailform.c ../../feckless_keyboard/src/feckless_keyboard.c
CFLAGS := -std=c11 -g -O1 -D_GNU_SOURCE -Wall -Wextra -fsanitize=address,undefined \
          -fno-sanitize-recover=all -fno-omit-frame-pointer $(INC)
WARNCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror $(INC)

all: run smtp warn

mailtest: $(SRC) ../include/mailcfg.h ../include/mailform.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -o $@ $(SRC) $(GFXSRC) -lm

run: mailtest
	./mailtest

smtptest: $(SMTPSRC) ../src/mailsmtp.h ../include/mailsend.h
	$(CC) $(CFLAGS) -I../src -o $@ $(SMTPSRC)

smtp: smtptest
	./smtptest

preview: mailtest
	./mailtest preview

warn:
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/mailcfg.c
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/mailform.c
	$(CC) $(WARNCFLAGS) -D_GNU_SOURCE -I../src -c -o /dev/null ../src/mailsmtp.c

clean:
	rm -f mailtest smtptest mail_*.ppm

.PHONY: all run preview warn clean
