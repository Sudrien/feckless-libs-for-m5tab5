/* SPDX-License-Identifier: MIT
 *
 * The host loop and its syscalls. No ESP-IDF; see feckless_umode_run.h.
 */
#include "feckless_umode_run.h"

#include <string.h>

bool feckless_umode_range_ok(const feckless_umode_window_t *w, uintptr_t addr, uint32_t len)
{
    if (w->hi <= w->lo) {
        return false;
    }
    /* No addition that can wrap: the program chose addr and len. */
    if (addr < w->lo || addr > w->hi) {
        return false;
    }
    return (uintptr_t)(w->hi - addr) >= len;
}

int feckless_umode_strlen(const feckless_umode_window_t *w, uintptr_t addr, uint32_t max)
{
    if (!feckless_umode_range_ok(w, addr, 1)) {
        return -1;
    }
    uintptr_t room = w->hi - addr;
    const uint32_t limit = room < max ? (uint32_t)room : max;
    const char *p = (const char *)addr;
    for (uint32_t n = 0; n < limit; n++) {
        if (p[n] == '\0') {
            return (int)n;
        }
    }
    return -1;
}

const char *feckless_umode_cause_name(uint32_t mcause)
{
    switch (mcause & FUMODE_MCAUSE_EXCCODE_MASK) {
    case FUMODE_EXC_INSN_MISALIGNED:  return "instruction address misaligned";
    case FUMODE_EXC_INSN_ACCESS:      return "instruction access fault";
    case FUMODE_EXC_ILLEGAL_INSN:     return "illegal instruction";
    case FUMODE_EXC_BREAKPOINT:       return "breakpoint";
    case FUMODE_EXC_LOAD_MISALIGNED:  return "load address misaligned";
    case FUMODE_EXC_LOAD_ACCESS:      return "load access fault";
    case FUMODE_EXC_STORE_MISALIGNED: return "store address misaligned";
    case FUMODE_EXC_STORE_ACCESS:     return "store access fault";
    case FUMODE_EXC_ECALL_U:          return "ecall from U-mode";
    case FUMODE_EXC_ECALL_M:          return "ecall from M-mode";
    default:                          return "unknown exception";
    }
}

/* One ecall. Returns true to resume the program. */
static bool dispatch(feckless_umode_window_t *w, const feckless_umode_ops_t *ops,
                     feckless_umode_end_t *end)
{
    feckless_umode_ctx_t *ctx = &w->ctx;
    const uint32_t nr = ctx->x[17];             /* a7 */
    const uint32_t *args = &ctx->x[10];         /* a0..a6 */
    uint32_t ret = 0;

    switch (nr) {
    case FECKLESS_SYS_NOP:
        break;
    case FECKLESS_SYS_WRITE:
        if (!ops->write) {
            ret = FECKLESS_SYS_ERR_BADNR;
        } else if (!feckless_umode_range_ok(w, (uintptr_t)args[0], args[1])) {
            ret = FECKLESS_SYS_ERR_FAULT;       /* recoverable: a bug in the program, not a violation */
        } else {
            ops->write((const char *)(uintptr_t)args[0], args[1]);
            ret = args[1];
        }
        break;
    case FECKLESS_SYS_DELAY_MS:
        if (ops->delay_ms) {
            ops->delay_ms(args[0]);             /* parked here, in task context: the scheduler runs on */
        } else {
            ret = FECKLESS_SYS_ERR_BADNR;
        }
        break;
    case FECKLESS_SYS_YIELD:
        if (ops->yield) {
            ops->yield();
        } else {
            ret = FECKLESS_SYS_ERR_BADNR;
        }
        break;
    case FECKLESS_SYS_GETCORE:
        ret = ops->core_id ? ops->core_id() : FECKLESS_SYS_ERR_BADNR;
        break;
    case FECKLESS_SYS_EXIT:
        end->reason = FECKLESS_UMODE_END_EXIT;
        end->status = args[0];
        end->pc = ctx->pc;
        return false;
    default:
        if (nr >= FECKLESS_SYS_EXT_BASE && w->ext) {
            if (!w->ext(w, nr, args, &ret)) {
                end->reason = FECKLESS_UMODE_END_KILLED;
                end->pc = ctx->pc;
                return false;
            }
        } else {
            ret = FECKLESS_SYS_ERR_BADNR;
        }
        break;
    }
    ctx->pc += 4;                               /* past the ecall */
    ctx->x[10] = ret;                           /* a0 */
    return true;
}

feckless_umode_end_t feckless_umode_run(feckless_umode_window_t *w, const feckless_umode_ops_t *ops)
{
    feckless_umode_end_t end;
    memset(&end, 0, sizeof end);

    if (!w || !ops || !ops->enter) {
        end.reason = FECKLESS_UMODE_END_REFUSED;
        return end;
    }
    /* The saved registers must not be the program's to rewrite. */
    const uintptr_t me = (uintptr_t)w;
    if (me < w->hi && me + sizeof *w > w->lo) {
        end.reason = FECKLESS_UMODE_END_REFUSED;
        return end;
    }

    w->core = ops->core_id ? (int)ops->core_id() : 0;
    for (;;) {
        const uint32_t cause = ops->enter(&w->ctx);

        /* Where did that window run? A different core from the last means
         * the scheduler moved this task in between. */
        if (ops->core_id) {
            const int core = (int)ops->core_id();
            if (core != w->core) {
                w->core = core;
                w->migrations++;
            }
        }

        /* Before the cause: the guard's verdict is independent of the trap
         * that ended the window -- the program left its stack at one
         * instruction and reached the host at a later, unrelated one. */
        if (w->ctx.sp_fired != 0) {
            end.reason = FECKLESS_UMODE_END_STACK;
            end.sp_pc = w->ctx.sp_pc;
            end.pc = w->ctx.pc;
            return end;
        }
        if (cause & FUMODE_MCAUSE_INTERRUPT) {
            /* Already serviced: umode.S put IDF's vector back before it
             * returned. Resume where the program was. */
            w->irqs++;
            continue;
        }
        if ((cause & FUMODE_MCAUSE_EXCCODE_MASK) == FUMODE_EXC_ECALL_U) {
            w->syscalls++;
            if (!dispatch(w, ops, &end)) {
                return end;
            }
            continue;
        }
        end.reason = FECKLESS_UMODE_END_FAULT;
        end.mcause = cause;
        end.mtval = w->ctx.mtval;
        end.pc = w->ctx.pc;
        return end;
    }
}
