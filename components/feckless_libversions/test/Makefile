# Host test for feckless_libversions: cmake/libversions.cmake in script
# mode against fixture/ -- a lock, managed components and a vendored one
# -- twice, as an app would call it, checked by gencheck.cmake: every row
# of libversions.h and libversions.tsv, and the header compiling.
#
#   make -C components/feckless_libversions/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-const-variable
GEN := ../../../cmake/libversions.cmake

all: gen

gen:
	rm -rf out && mkdir -p out/full out/noidf
	cd out/full && cmake -DROOT=../../fixture -DLV_NAME=Fixture \
	  "-DLV_EXTRA=World map, (c) OSM \"contributors\"|2026-10-31|ODbL-1.0;MurmurHash2|-|public domain;Bare" \
	  -P $(GEN)
	cd out/noidf && cmake -DROOT=../../fixture -DLV_NAME=Fixture -DLV_NO_IDF=ON -P $(GEN)
	cmake -DDIR=out/full -DNO_IDF=OFF -P gencheck.cmake
	cmake -DDIR=out/noidf -DNO_IDF=ON -P gencheck.cmake
	printf '#include "libversions.h"\n#include "feckless_libversions.h"\nint main(void){return k_libversions[0].module[0] != 70 || LIBVERSIONS_COUNT < 5;}\n' > out/use.c
	$(CC) $(CFLAGS) -I../include -Iout/full/libversions -o out/use out/use.c && ./out/use

clean:
	rm -rf out

.PHONY: all gen clean
