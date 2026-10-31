# Host tests for feckless_image_handler, as the sources are:
#   imagetest    feckless_image.h and image_ico.c            (7004)
#   stbjpegtest  image_stb.c: JPEG and GIF through stb_image (7005)
#   gifimgtest   image_stb.c: a GIF's first frame             (7005)
#   jxltest      image_jxl.c: JPEG XL through J40            (7005)
#   pngimgtest   image_png.c: PNG through pngle              (7006)
#   decodetest   image_decode.c: the order, with fake decoders (7007)
#   fittest      image_fit.c: fit to a box, blocks, strides   (7009)
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
PNGLE_COMMIT := b1c68193f1d3f8642b3e0e095d457a828038e6fb
PNGLE_FILES  := pngle.c pngle.h miniz.c miniz.h
PNGLE_SRC    := ../vendor/pngle.c ../vendor/miniz.c
PNGLE_SHA_pngle.c := cfd047df6464ac299ecd47aeb13fecfb55f1d841da56dc57085248ef43655049
PNGLE_SHA_pngle.h := d8aa264f80cdd884c02ca1e28b696e8bbfbc16074c9c5265fcec4463f563bb17
PNGLE_SHA_miniz.c := 97efe2132abc8346a3d11f29f84b574f5da78a1b94432ea42b0c754c7c39da4e
PNGLE_SHA_miniz.h := 8a638511f9f98e8240fc00522989fe7908a06f73a6386191e4374046c993cfa0

# Upstream's warnings are upstream's: the same list ../CMakeLists.txt uses.
STB_FLAGS := -I../vendor -Wno-sign-compare -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-type-limits -Wno-shift-negative-value \
             -Wno-implicit-fallthrough -fwrapv -fno-sanitize=signed-integer-overflow
J40_FLAGS := -I../vendor -Wno-sign-compare -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-type-limits -Wno-implicit-fallthrough \
             -Wno-unused-variable -Wno-maybe-uninitialized -Wno-missing-field-initializers \
             -fwrapv -fno-sanitize=signed-integer-overflow

all: run run-stbjpeg run-gif run-jxl run-png run-decode run-fit

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

# pngle.c, pngle.h, miniz.c and miniz.h: each pinned and checked.
../vendor/pngle.c ../vendor/pngle.h ../vendor/miniz.c ../vendor/miniz.h:
	mkdir -p ../vendor
	f=$(notdir $@); \
	curl -fsSL "https://raw.githubusercontent.com/kikuchan/pngle/$(PNGLE_COMMIT)/src/$$f" -o $@.tmp && \
	want=$$(case $$f in pngle.c) echo $(PNGLE_SHA_pngle.c);; pngle.h) echo $(PNGLE_SHA_pngle.h);; \
	        miniz.c) echo $(PNGLE_SHA_miniz.c);; miniz.h) echo $(PNGLE_SHA_miniz.h);; esac) && \
	echo "$$want  $@.tmp" | sha256sum -c - >/dev/null || (rm -f $@.tmp; echo "$$f: fetch or hash failed"; exit 1); \
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

# miniz stores through a misaligned pointer (upstream's, and fine on the
# RISC-V and x86 it targets); the flag goes after LDFLAGS, whose
# -fsanitize would turn the check back on. Defeatist's arttest did the same.
PNGLE_FLAGS := -I../vendor -Wno-sign-compare -Wno-unused-function -Wno-unused-parameter -fwrapv -fno-sanitize=signed-integer-overflow \
               -Wno-attributes

pngimgtest: pngimgtest.c ../src/image_png.c ../include/feckless_image_png.h ../include/feckless_image.h $(PNGLE_SRC) ../vendor/pngle.h ../vendor/miniz.h
	$(CC) $(WARNCFLAGS) -c pngimgtest.c -o /dev/null
	$(CC) $(CFLAGS) $(PNGLE_FLAGS) -o $@ pngimgtest.c ../src/image_png.c $(PNGLE_SRC) $(LDFLAGS) -fno-sanitize=alignment

run-png: pngimgtest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./pngimgtest

decodetest: decodetest.c ../src/image_decode.c ../src/image_stb.c ../src/image_jxl.c ../include/feckless_image_decode.h $(STB_H) $(J40_H)
	$(CC) $(WARNCFLAGS) -c decodetest.c -o /dev/null
	$(CC) $(CFLAGS) $(STB_FLAGS) -I../vendor -c ../src/image_decode.c -o $@-core.o
	$(CC) $(CFLAGS) $(STB_FLAGS) -c ../src/image_stb.c -o $@-stb.o
	$(CC) $(CFLAGS) $(J40_FLAGS) -c ../src/image_jxl.c -o $@-jxl.o
	$(CC) $(CFLAGS) -o $@ decodetest.c $@-core.o $@-stb.o $@-jxl.o $(LDFLAGS)
	rm -f $@-core.o $@-stb.o $@-jxl.o

run-decode: decodetest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./decodetest

fittest: fittest.c ../src/image_fit.c ../include/feckless_image_fit.h
	$(CC) $(CFLAGS) -o $@ fittest.c ../src/image_fit.c $(LDFLAGS)

run-fit: fittest
	./fittest

jxltest: jxltest.c ../src/image_jxl.c ../include/feckless_image_jxl.h ../include/feckless_image.h $(J40_H)
	$(CC) $(WARNCFLAGS) -c jxltest.c -o /dev/null
	$(CC) $(CFLAGS) $(J40_FLAGS) -o $@ jxltest.c ../src/image_jxl.c $(LDFLAGS)

run-jxl: jxltest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./jxltest

clean:
	rm -f imagetest stbjpegtest gifimgtest jxltest pngimgtest decodetest fittest
.PHONY: all run run-stbjpeg run-gif run-jxl run-png run-decode run-fit clean
