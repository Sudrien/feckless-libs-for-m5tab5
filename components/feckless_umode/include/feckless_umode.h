/*
 * umode.h -- kernel-side interface to the U-mode execution window.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Derived from main/umode.h of NevynUK/ESP32-P4-Protected-Mode-Sample (see
 * NOTICE): names prefixed feckless_ / FUMODE_, the PSRAM-grant call removed,
 * the grant planner and ctx helper below added.
 *
 * The ESP32-P4 core is an RV32 with M and U privilege modes.  ESP-IDF and
 * FreeRTOS live entirely in M-mode; this header describes the machinery that
 * drops out of M-mode into U-mode to run the user application, and that brings
 * control back to the kernel when the user traps.
 *
 * The context block below is shared with umode.S, so the byte offsets are
 * spelled out as macros and checked against the C struct with _Static_asserts
 * in kernel_main.c.  Change one and the assert catches the other.
 *
 * It carries four groups of fields, and it is worth knowing which is which
 * before changing any of them:
 *
 *   x[]/pc/mcause/mtval   the user's machine state.  Loaded into the core by
 *                         feckless_umode_enter(), written back by the trap vector.
 *   k_*                   kernel state parked for the length of the window
 *                         and restored on the way out.  Written by
 *                         feckless_umode_enter(); nothing else should touch it.
 *   u_sp_min/u_sp_max     configuration, written by the CALLER before
 *                         feckless_umode_enter() is called.
 *   sp_fired/sp_pc        results, written by the trap vector for the caller
 *                         to read after feckless_umode_enter() returns.
 */

#ifndef FECKLESS_UMODE_H
#define FECKLESS_UMODE_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Offsets into feckless_umode_ctx_t.  FUMODE_CTX_X(n) is RISC-V register x<n>; x0 is not
 * stored but the slot is kept so the index is the register number.
 */

#define FUMODE_CTX_X(n)            ((n) * 4)

#define FUMODE_CTX_PC              128    /* user pc (mepc)                       */
#define FUMODE_CTX_MCAUSE          132    /* mcause of the trap that came back    */
#define FUMODE_CTX_MTVAL           136    /* mtval of that trap                   */

/* Kernel state parked across the U-mode window */

#define FUMODE_CTX_K_SP            140
#define FUMODE_CTX_K_MTVEC         144
#define FUMODE_CTX_K_MTVT          148
#define FUMODE_CTX_K_MSCRATCH      152
#define FUMODE_CTX_K_MSTATUS       156
#define FUMODE_CTX_K_MINTSTATUS    160
#define FUMODE_CTX_K_THRESH        164
#define FUMODE_CTX_K_SP_MIN        168    /* the host task's stack-guard bounds    */
#define FUMODE_CTX_K_SP_MAX        172

/* The stack ESP-IDF's hardware stack guard watches while the window is open.
 * Filled in by the caller, not by feckless_umode_enter().
 */

#define FUMODE_CTX_U_SP_MIN        176
#define FUMODE_CTX_U_SP_MAX        180

/* The guard's verdict on the user's stack, written by the trap vector */

#define FUMODE_CTX_SP_FIRED        184    /* nonzero if the user left its stack    */
#define FUMODE_CTX_SP_PC           188    /* the pc assist_debug latched, if so    */

#define FUMODE_CTX_SIZE            192

/* The pre-rev3 ESP32-P4 keeps the CLIC interrupt threshold in a MEMORY-MAPPED
 * register rather than a CSR (INTTHRESH_STANDARD = 0).  Raising it to the
 * maximum masks every interrupt source; exceptions are not gated by it, so
 * `ecall` still arrives.  This is the only way to keep interrupts out of the
 * U-mode window: mstatus.MIE does not apply while the core is in U-mode.
 *
 * Spelled out rather than taken from soc/clic_reg.h because umode.S needs it
 * and that header is not assembler-clean.  kernel_main.c static_asserts the
 * two against each other, so a change in IDF is caught at compile time.
 */

#define FUMODE_CLIC_THRESH_REG   0x20800008
#define FUMODE_CLIC_THRESH_MASK  0xff000000

/* u-feckless-0016: the CLIC's per-line control words, core-local: line id's
 * word at FUMODE_CLIC_INT_CTRL + 4 * id (IDF's CLIC_INT_CTRL_REG(id)). Bit 0
 * is pending, bits 18:17 the trigger (bit 17 set: edge). umode.S re-pends an
 * edge-triggered interrupt that a window took. */
#define FUMODE_CLIC_INT_CTRL    0x20801000

/* mstatus fields */

#define FUMODE_MSTATUS_MIE          0x00000008
#define FUMODE_MSTATUS_MPIE         0x00000080
#define FUMODE_MSTATUS_MPP          0x00001800   /* 3 = M, 0 = U */

/* mcause fields.  The pre-rev3 ESP32-P4 CLIC packs the previous privilege,
 * previous interrupt-enable and previous interrupt level into mcause:
 *
 *   [31]    interrupt
 *   [29:28] mpp    -- write-through aliased to mstatus.MPP, so whichever of
 *                     the two is written LAST wins for the privilege an mret
 *                     returns to
 *   [27]    mpie
 *   [23:16] mpil   -- mret restores mintstatus.mil from this
 *   [11:0]  exccode
 */

#define FUMODE_MCAUSE_INTERRUPT     0x80000000
#define FUMODE_MCAUSE_MPP_M         0x30000000
#define FUMODE_MCAUSE_MPIE          0x08000000
#define FUMODE_MCAUSE_MPIL_MASK     0x00ff0000
#define FUMODE_MCAUSE_EXCCODE_MASK  0x00000fff

/* Exception codes we care about */

#define FUMODE_EXC_INSN_MISALIGNED  0
#define FUMODE_EXC_INSN_ACCESS      1
#define FUMODE_EXC_ILLEGAL_INSN     2
#define FUMODE_EXC_BREAKPOINT       3
#define FUMODE_EXC_LOAD_MISALIGNED  4
#define FUMODE_EXC_LOAD_ACCESS      5
#define FUMODE_EXC_STORE_MISALIGNED 6
#define FUMODE_EXC_STORE_ACCESS     7
#define FUMODE_EXC_ECALL_U          8    /* the syscall path                       */
#define FUMODE_EXC_ECALL_M          11

#ifndef __ASSEMBLER__

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One user thread's machine state.  feckless_umode_enter() loads the x[] array and pc
 * into the core and drops to U-mode; the trap vector writes them back when the
 * user traps and returns to the kernel.
 */

typedef struct
{
  uint32_t x[32];              /* x[0] unused, x[1]..x[31]                  */
  uint32_t pc;                 /* where U-mode resumes                      */
  uint32_t mcause;             /* filled in by the trap vector              */
  uint32_t mtval;              /* filled in by the trap vector              */

  /* Everything below belongs to the kernel and is written by feckless_umode_enter() */

  uint32_t k_sp;
  uint32_t k_mtvec;
  uint32_t k_mtvt;
  uint32_t k_mscratch;
  uint32_t k_mstatus;
  uint32_t k_mintstatus;
  uint32_t k_thresh;
  uint32_t k_sp_min;           /* the host task's stack-guard bounds        */
  uint32_t k_sp_max;

  /* The stack ESP-IDF's hardware stack guard watches while the window is
   * open.  Written by the CALLER before feckless_umode_enter(), not by feckless_umode_enter().
   * The guard is otherwise armed on the host task's stack, which the U-mode
   * stack switch leaves by construction.
   */

  uint32_t u_sp_min;
  uint32_t u_sp_max;

  /* The guard's verdict, written by the trap vector on the way out.  Nonzero
   * sp_fired means the user's sp left [u_sp_min, u_sp_max] at some point in
   * the window; sp_pc is where.
   */

  uint32_t sp_fired;
  uint32_t sp_pc;
} feckless_umode_ctx_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* feckless_umode_enter -- run the user context until it traps.
 *
 * Saves the kernel's callee-saved registers and trap vectors, installs the
 * private vector in umode.S, and mrets to U-mode at ctx->pc.  Returns when the
 * user takes any trap at all -- syscall, fault or interrupt -- with the user's
 * registers written back into ctx.  The return value is ctx->mcause.
 *
 * Must be called from a FreeRTOS task, in M-mode, with a valid ctx.
 *
 * The caller must have set ctx->u_sp_min and ctx->u_sp_max: the range the
 * hardware stack guard watches for the length of the window.  feckless_umode_enter()
 * installs whatever is there, zeros included.
 *
 * On return, check ctx->sp_fired BEFORE acting on the return value.  A guard
 * violation is independent of the trap that ended the window -- the user
 * leaves its stack at one instruction and reaches the kernel at a later,
 * unrelated one -- so a nonzero sp_fired can accompany a perfectly ordinary
 * syscall cause.  ctx->sp_pc is the instruction the hardware latched.
 */

uint32_t feckless_umode_enter(feckless_umode_ctx_t *ctx);

/* CSR reads.  These live in umode.S rather than as inline asm so that every
 * instruction in this project is in a .S file.
 */

uint32_t feckless_umode_read_mstatus(void);
uint32_t feckless_umode_read_mtvec(void);
uint32_t feckless_umode_read_mintstatus(void);
uint32_t feckless_umode_read_pmpcfg(uint32_t idx);
uint32_t feckless_umode_read_pmpaddr(uint32_t idx);

/* Write all sixteen PMP entries.  addr[] values are pre-shifted right by two;
 * cfg[] is the four packed pmpcfg words.  Per-hart: call it on every core, and
 * only useful while the entries are unlocked.
 */

void feckless_umode_pmp_program(const uint32_t addr[16], const uint32_t cfg[4]);

/* ---- what U-mode may touch ---------------------------------------------- */

/* U-mode starts with access to nothing: PMP is default-deny for U-mode and
 * default-allow for M-mode, and this component requires the PMP to be in its
 * reset state (CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n; the component's
 * CMakeLists.txt stops the configure otherwise).  A grant gives U-mode one
 * range back.  None is locked, so none applies to M-mode.
 *
 * Each grant costs two of the sixteen entries (a TOR pair: the first holds
 * the base and is OFF, the second the limit and the permissions), so at most
 * FECKLESS_UMODE_MAX_GRANTS.  The lowest-numbered entry that matches an
 * address decides, so where grants overlap the earlier one in the list wins.
 */

#define FECKLESS_UMODE_R           1u
#define FECKLESS_UMODE_W           2u
#define FECKLESS_UMODE_X           4u
#define FECKLESS_UMODE_MAX_GRANTS  8

typedef struct
{
  uint32_t lo;                 /* first byte; a multiple of 4              */
  uint32_t hi;                 /* one past the last byte; a multiple of 4  */
  uint32_t perm;               /* FECKLESS_UMODE_R | _W | _X; W needs R    */
} feckless_umode_grant_t;

#define FECKLESS_UMODE_OK            0
#define FECKLESS_UMODE_ERR_COUNT    (-1)   /* more than MAX_GRANTS, or NULL  */
#define FECKLESS_UMODE_ERR_RANGE    (-2)   /* lo >= hi                       */
#define FECKLESS_UMODE_ERR_ALIGN    (-3)   /* lo or hi not a multiple of 4   */
#define FECKLESS_UMODE_ERR_PERM     (-4)   /* none, unknown bits, or W alone */

/* Turn grants into the sixteen pmpaddr values (already shifted right by two)
 * and the four packed pmpcfg words that feckless_umode_pmp_program() takes.
 * Entries past the grants are all zero: OFF.  Touches no CSR, so it runs
 * anywhere, and the host test runs it.  Returns FECKLESS_UMODE_OK or one of
 * the errors above, writing nothing to addr or cfg on an error.
 */

int feckless_umode_pmp_plan(const feckless_umode_grant_t *grants, int n,
                            uint32_t addr[16], uint32_t cfg[4]);

/* Make a context ready for feckless_umode_enter(): all registers zero but
 * sp, a0 and pc, and the stack-guard range set.  [stack_lo, stack_hi] is the
 * user's stack, the upper bound inclusive: sp starts at stack_hi.
 */

void feckless_umode_ctx_init(feckless_umode_ctx_t *ctx, uint32_t pc,
                             uint32_t stack_lo, uint32_t stack_hi, uint32_t a0);

#endif /* __ASSEMBLER__ */
#endif /* FECKLESS_UMODE_H */
