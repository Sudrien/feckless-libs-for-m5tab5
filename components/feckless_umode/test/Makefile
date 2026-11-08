# Host checks for feckless_umode.
#
#   make -C components/feckless_umode/test           the plan, run-loop and box tests
#   make -C components/feckless_umode/test asm       umode.S assembles, with the stack guard on and off
#   make -C components/feckless_umode/test compare SAMPLE=<dir of the sample's umode.S>
#                                                    umode_enter, the trap entry and the rest are the
#                                                    same instructions as the sample's
#
# asm and compare need clang with the riscv32 target and llvm-objdump. The
# stack guard's macros in fake/ are IDF 6.1's own (u-feckless-0010), so a
# call with the wrong arguments fails here as in an IDF build.
#
# SPDX-License-Identifier: MIT

CC ?= cc
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-missing-field-initializers \
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

umode_run_test: umode_run_test.c ../src/umode_run.c ../include/feckless_umode_run.h ../include/feckless_umode_sys.h ../include/feckless_umode.h
	$(CC) $(CFLAGS) -o $@ umode_run_test.c ../src/umode_run.c $(LDFLAGS)

umode_box_test: umode_box_test.c ../src/umode_box.c ../src/umode_calls.c ../include/feckless_umode_box.h ../include/feckless_umode_calls.h
	$(CC) $(CFLAGS) -o $@ umode_box_test.c ../src/umode_box.c ../src/umode_calls.c $(LDFLAGS)

run: umode_plan_test umode_run_test umode_box_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./umode_plan_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./umode_run_test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./umode_box_test

asm:
	$(CLANG) $(RV) $(ASFLAGS) -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=0 -c ../src/umode.S -o umode_noguard.o
	$(CLANG) $(RV) $(ASFLAGS) -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=1 -c ../src/umode.S -o umode_guard.o
	@echo "umode.S assembles, guard off and on"
	$(CLANG) $(RV) -std=c11 -O1 -Wall -Wextra -Werror -ffreestanding -I../include -S user_stub.c -o user_stub.s
	@grep -q "li.a7, 1$$" user_stub.s && grep -q "li.a7, 4$$" user_stub.s && [ $$(grep -c "^	ecall" user_stub.s) -eq 2 ] \
	  && echo "the user stubs are an ecall with the number in a7"
	$(MAKE) --no-print-directory umode_box_test && ./umode_box_test >/dev/null
	$(CLANG) $(RV) -c stubs_expected.S -o stubs_expected.o
	llvm-objcopy -O binary -j .text stubs_expected.o stubs_expected.bin
	@cmp stubs.bin stubs_expected.bin && echo "the box's stubs are the assembler's instructions"

# Per function, addresses stripped and the feckless_ prefix dropped, so a
# difference is a difference in an instruction.
define BODY
$(OBJDUMP) -dr --no-show-raw-insn $(1) | sed -E 's/feckless_//g; s/^ *[0-9a-f]+:[[:space:]]+//; s/0x[0-9a-f]+ //; s/[0-9a-f]{8} <(.*)>:$$/<\1>:/' | awk -v f="<$(2)>:" '$$0==f{p=1;next} /^<[^.].*>:$$/{p=0} p'
endef

compare: asm
	@test -n "$(SAMPLE)" || { echo "SAMPLE=<dir with umode.S and umode.h> is needed"; exit 2; }
	@# The sample is IDF 5.5's: its SET_BOUNDS call names one register and the
	@# macro read a0 and a1. IDF 6.1's names all three (u-feckless-0010); the
	@# copy assembled here says so, and the instructions are then compared.
	sed 's/ESP_HW_STACK_GUARD_SET_BOUNDS_CUR_CORE t0$$/ESP_HW_STACK_GUARD_SET_BOUNDS_CUR_CORE t0 a0 a1/' $(SAMPLE)/umode.S > sample_umode.S
	$(CLANG) $(RV) -Ifake -I$(SAMPLE) -x assembler-with-cpp -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=1 -c sample_umode.S -o sample_guard.o
	$(CLANG) $(RV) -Ifake -I$(SAMPLE) -x assembler-with-cpp -DCONFIG_ESP_SYSTEM_HW_STACK_GUARD=0 -c sample_umode.S -o sample_noguard.o
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
	rm -f umode_plan_test umode_run_test umode_box_test *.o *.bin user_stub.s sample_umode.S ours.txt theirs.txt
.PHONY: all run asm compare clean
