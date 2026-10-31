# Host tests for feckless_image_handler, as the sources are:
#   imagetest    feckless_image.h and image_ico.c            (7004)
#   stbjpegtest  image_stb.c: JPEG and GIF through stb_image (7005)
#   gifimgtest   image_stb.c: a GIF's first frame             (7005)
#   jxltest      image_jxl.c: JPEG XL through J40            (7005)
#
#   make -C components/feckless_image_handler/test
#
# stb_image.h and j40.h are fetched into ../vendor/ (gitignored), pinned
# and hash-checked, as ../cmake/vendored.cmake does for a build.
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer -I../include
LDFLAGS := -fsanitize=address,undefined -lm
WARNCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I../include

STB_COMMIT := 013ac3beddff3dbffafd5177e7972067cd2b5083
STB_SHA    := 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
J40_COMMIT := 252e7987d36d50f617f29e94eeeb741198adc617
J40_SHA    := 981c80c07d46d7a263d7790e71f6d9713278974088235cffcf21dea4fc3b5f6d
STB_H := ../vendor/stb_image.h
J40_H := ../vendor/j40.h

# Upstream's warnings are upstream's: the same list ../CMakeLists.txt uses.
STB_FLAGS := -I../vendor -Wno-sign-compare -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-type-limits -Wno-shift-negative-value \
             -Wno-implicit-fallthrough -fwrapv -fno-sanitize=signed-integer-overflow
J40_FLAGS := -I../vendor -Wno-sign-compare -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-type-limits -Wno-implicit-fallthrough \
             -Wno-unused-variable -Wno-maybe-uninitialized -Wno-missing-field-initializers \
             -fwrapv -fno-sanitize=signed-integer-overflow

all: run run-stbjpeg run-gif run-jxl

$(STB_H):
	mkdir -p ../vendor
	curl -fsSL "https://raw.githubusercontent.com/nothings/stb/$(STB_COMMIT)/stb_image.h" -o $@.tmp
	echo "$(STB_SHA)  $@.tmp" | sha256sum -c - >/dev/null || (rm -f $@.tmp; echo "stb_image.h: hash mismatch"; exit 1)
	mv $@.tmp $@

$(J40_H):
	mkdir -p ../vendor
	curl -fsSL "https://raw.githubusercontent.com/lifthrasiir/j40/$(J40_COMMIT)/j40.h" -o $@.tmp
	echo "$(J40_SHA)  $@.tmp" | sha256sum -c - >/dev/null || (rm -f $@.tmp; echo "j40.h: hash mismatch"; exit 1)
	mv $@.tmp $@

imagetest: imagetest.c ../src/image_ico.c ../include/feckless_image.h
	$(CC) $(CFLAGS) -o $@ imagetest.c ../src/image_ico.c $(LDFLAGS)

run: imagetest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./imagetest

stbjpegtest: stbjpegtest.c ../src/image_stb.c ../include/feckless_image_stb.h $(STB_H)
	$(CC) $(WARNCFLAGS) -c stbjpegtest.c -o /dev/null
	$(CC) $(CFLAGS) $(STB_FLAGS) -o $@ stbjpegtest.c ../src/image_stb.c $(LDFLAGS)

run-stbjpeg: stbjpegtest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./stbjpegtest

gifimgtest: gifimgtest.c ../src/image_stb.c ../include/feckless_image_stb.h ../include/feckless_image.h $(STB_H)
	$(CC) $(WARNCFLAGS) -c gifimgtest.c -o /dev/null
	$(CC) $(CFLAGS) $(STB_FLAGS) -o $@ gifimgtest.c ../src/image_stb.c $(LDFLAGS)

run-gif: gifimgtest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./gifimgtest

jxltest: jxltest.c ../src/image_jxl.c ../include/feckless_image_jxl.h ../include/feckless_image.h $(J40_H)
	$(CC) $(WARNCFLAGS) -c jxltest.c -o /dev/null
	$(CC) $(CFLAGS) $(J40_FLAGS) -o $@ jxltest.c ../src/image_jxl.c $(LDFLAGS)

run-jxl: jxltest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./jxltest

clean:
	rm -f imagetest stbjpegtest gifimgtest jxltest
.PHONY: all run run-stbjpeg run-gif run-jxl clean
