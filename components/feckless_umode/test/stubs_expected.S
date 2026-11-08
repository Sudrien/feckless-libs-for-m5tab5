/* What feckless_box_write_stubs(dst, 3) must write, as the assembler writes it.
 * SPDX-License-Identifier: MIT */
  .option norvc
  mv t0, a7
  addi a7, x0, 16
  ecall
  ret
  mv t0, a7
  addi a7, x0, 17
  ecall
  ret
  mv t0, a7
  addi a7, x0, 18
  ecall
  ret
  addi a7, x0, 4
  addi a0, x0, 0
  ecall
1: j 1b
