# Host test for feckless_storage_handler's lister (9011):
#   listtest  storage_list.c against a real folder in /tmp, under ASan
#             and UBSan: what is left out, the filter, the three orders,
#             pinned rows, max, the volumes, a growing arena
#   same      the test's copies of storage_join_path() and
#             storage_is_hidden() are storage.c's, byte for byte
#   warn      storage_list.c as the firmware compiles it, -O2 and every
#             warning an error -- the host half; the FatFs half needs
#             IDF's headers and is not built here
#
#   make -C components/feckless_storage_handler/test
#
# SPDX-License-Identifier: MIT

CC ?= cc
INC := -I../include -Ifake
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror -DSTORAGE_LIST_HOST \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer $(INC)
LDFLAGS := -fsanitize=address,undefined
WARNCFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -DSTORAGE_LIST_HOST $(INC)

all: run same warn

listtest: listtest.c ../src/storage_list.c ../include/storage_list.h ../include/storage.h
	$(CC) $(CFLAGS) -o $@ listtest.c ../src/storage_list.c $(LDFLAGS)

run: listtest
	./listtest

# The two functions, cut from each file from their signature to the
# closing brace, compared.
CUT := awk -v f="$$f" '$$0 ~ "^bool " f "\\(" {p=1} p {print} p && /^}/ {exit}'

same:
	@for f in storage_join_path storage_is_hidden; do \
	    $(CUT) ../src/storage.c > .a; $(CUT) listtest.c > .b; \
	    test -s .a && cmp -s .a .b || { echo "listtest.c's $$f is not storage.c's"; rm -f .a .b; exit 1; }; \
	done; rm -f .a .b; echo "same: ok"

warn:
	$(CC) $(WARNCFLAGS) -c -o /dev/null ../src/storage_list.c

clean:
	rm -f listtest .a .b

.PHONY: all run same warn clean
