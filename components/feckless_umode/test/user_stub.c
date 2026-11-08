/* SPDX-License-Identifier: MIT
 * What a U-mode program calls the host with: compiled for riscv32 by `make asm`,
 * which checks the output is an ecall with the number in a7 and the arguments
 * in a0..a2. */
#include "feckless_umode_sys.h"

uint32_t user_write(uint32_t buf, uint32_t len)
{
    return feckless_umode_syscall2(FECKLESS_SYS_WRITE, buf, len);
}

void user_exit(uint32_t status)
{
    feckless_umode_syscall1(FECKLESS_SYS_EXIT, status);
}
