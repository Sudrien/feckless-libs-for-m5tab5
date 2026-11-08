# Host checks for feckless_umode.
#
#   make -C components/feckless_umode/test           the plan test
#   make -C components/feckless_umode/test asm       umode.S assembles, with the stack guard on and off
#   make -C components/feckless_umode/test compare SAMPLE=<dir of the sample's umode.S>
#                                                    umode_enter, the trap entry and the rest are the
#                                                    same instructions as the sample's
#
# asm and compare need clang with the riscv32 target and llvm-objdump.
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all \
          -fno-omit-frame-pointer -I../include
LDFLAGS := -fsanitize=address,undefined

CLANG ?= clang
OBJDUMP ?= llvm-objdump
RV := --target=riscv32-unknown-elf -march=rv32imafc -mabi=ilp32f
ASFLAGS := -Ifake -I../include -x assembler-with-cpp

all: run

umode_plan_test: umode_plan_test.c ../src/umode_plan.c ../include/feckless_umode.h
	$(CC) $(CFLAGS) -o $@ umode_plan_test.c ../src/umode_plan.c $(LDFLAGS)

run: umode_plan_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./umode_plan_test

asm:
	$(CLANG) $(RV) $(ASFLAGS) -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=0 -c ../src/umode.S -o umode_noguard.o
	$(CLANG) $(RV) $(ASFLAGS) -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=1 -c ../src/umode.S -o umode_guard.o
	@echo "umode.S assembles, guard off and on"

# Per function, addresses stripped and the feckless_ prefix dropped, so a
# difference is a difference in an instruction.
define BODY
$(OBJDUMP) -dr --no-show-raw-insn $(1) | sed -E 's/feckless_//g; s/^ *[0-9a-f]+:[[:space:]]+//; s/0x[0-9a-f]+ //; s/[0-9a-f]{8} <(.*)>:$$/<\1>:/' | awk -v f="<$(2)>:" '$$0==f{p=1;next} /^<[^.].*>:$$/{p=0} p'
endef

compare: asm
	@test -n "$(SAMPLE)" || { echo "SAMPLE=<dir with umode.S and umode.h> is needed"; exit 2; }
	$(CLANG) $(RV) -Ifake -I$(SAMPLE) -x assembler-with-cpp -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=1 -c $(SAMPLE)/umode.S -o sample_guard.o
	$(CLANG) $(RV) -Ifake -I$(SAMPLE) -x assembler-with-cpp -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=0 -c $(SAMPLE)/umode.S -o sample_noguard.o
	@set -e; for g in guard noguard; do \
	  for f in umode_enter umode_trap_entry umode_read_mstatus umode_read_mtvec umode_read_mintstatus \
	           umode_read_pmpcfg umode_read_pmpaddr umode_pmp_program; do \
	    $(call BODY,umode_$$g.o,$$f) > ours.txt; \
	    $(call BODY,sample_$$g.o,$$f) > theirs.txt; \
	    test -s ours.txt || { echo "no body for $$f ($$g)"; exit 1; }; \
	    cmp -s ours.txt theirs.txt || { echo "DIFFERS: $$f ($$g)"; diff ours.txt theirs.txt | head; exit 1; }; \
	  done; \
	done; rm -f ours.txt theirs.txt; echo "same instructions as the sample, guard off and on"

clean:
	rm -f umode_plan_test *.o ours.txt theirs.txt
.PHONY: all run asm compare clean
