/* SPDX-License-Identifier: MIT
 *
 * The parts of feckless_umode that need no ESP-IDF and touch no CSR: the
 * PMP plan for a list of grants, and a context made ready to run. The host
 * test (test/) builds this file as it is.
 */
#include "feckless_umode.h"

#include <stddef.h>
#include <string.h>

/* umode.S addresses the context by these byte offsets. */
_Static_assert(offsetof(feckless_umode_ctx_t, x) == FUMODE_CTX_X(0), "ctx.x moved");
_Static_assert(offsetof(feckless_umode_ctx_t, pc) == FUMODE_CTX_PC, "ctx.pc moved");
_Static_assert(offsetof(feckless_umode_ctx_t, mcause) == FUMODE_CTX_MCAUSE, "ctx.mcause moved");
_Static_assert(offsetof(feckless_umode_ctx_t, mtval) == FUMODE_CTX_MTVAL, "ctx.mtval moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_sp) == FUMODE_CTX_K_SP, "ctx.k_sp moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_mtvec) == FUMODE_CTX_K_MTVEC, "ctx.k_mtvec moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_mtvt) == FUMODE_CTX_K_MTVT, "ctx.k_mtvt moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_mscratch) == FUMODE_CTX_K_MSCRATCH, "ctx.k_mscratch moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_mstatus) == FUMODE_CTX_K_MSTATUS, "ctx.k_mstatus moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_mintstatus) == FUMODE_CTX_K_MINTSTATUS, "ctx.k_mintstatus moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_thresh) == FUMODE_CTX_K_THRESH, "ctx.k_thresh moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_sp_min) == FUMODE_CTX_K_SP_MIN, "ctx.k_sp_min moved");
_Static_assert(offsetof(feckless_umode_ctx_t, k_sp_max) == FUMODE_CTX_K_SP_MAX, "ctx.k_sp_max moved");
_Static_assert(offsetof(feckless_umode_ctx_t, u_sp_min) == FUMODE_CTX_U_SP_MIN, "ctx.u_sp_min moved");
_Static_assert(offsetof(feckless_umode_ctx_t, u_sp_max) == FUMODE_CTX_U_SP_MAX, "ctx.u_sp_max moved");
_Static_assert(offsetof(feckless_umode_ctx_t, sp_fired) == FUMODE_CTX_SP_FIRED, "ctx.sp_fired moved");
_Static_assert(offsetof(feckless_umode_ctx_t, sp_pc) == FUMODE_CTX_SP_PC, "ctx.sp_pc moved");
_Static_assert(sizeof(feckless_umode_ctx_t) == FUMODE_CTX_SIZE, "ctx size changed");

#define PMP_A_TOR  (1u << 3)
#define PMP_RWX    (FECKLESS_UMODE_R | FECKLESS_UMODE_W | FECKLESS_UMODE_X)

int feckless_umode_pmp_plan(const feckless_umode_grant_t *grants, int n,
                            uint32_t addr[16], uint32_t cfg[4])
{
    if (n < 0 || n > FECKLESS_UMODE_MAX_GRANTS || (n > 0 && !grants)) {
        return FECKLESS_UMODE_ERR_COUNT;
    }
    for (int i = 0; i < n; i++) {
        const feckless_umode_grant_t *g = &grants[i];
        if (g->lo >= g->hi) {
            return FECKLESS_UMODE_ERR_RANGE;
        }
        if ((g->lo | g->hi) & 3u) {
            return FECKLESS_UMODE_ERR_ALIGN;
        }
        /* W without R is a reserved combination in the PMP. */
        if (g->perm == 0 || (g->perm & ~PMP_RWX) != 0 ||
            ((g->perm & FECKLESS_UMODE_W) && !(g->perm & FECKLESS_UMODE_R))) {
            return FECKLESS_UMODE_ERR_PERM;
        }
    }

    uint8_t byte[16] = { 0 };
    for (int i = 0; i < 16; i++) {
        addr[i] = 0;
    }
    for (int i = 0; i < n; i++) {
        /* A TOR entry spans from the previous entry's address to its own:
         * the even entry carries the base and stays OFF, the odd one the
         * limit and the permissions. */
        addr[2 * i] = grants[i].lo >> 2;
        addr[2 * i + 1] = grants[i].hi >> 2;
        byte[2 * i + 1] = (uint8_t)(PMP_A_TOR | grants[i].perm);
    }
    for (int w = 0; w < 4; w++) {
        cfg[w] = (uint32_t)byte[4 * w] | ((uint32_t)byte[4 * w + 1] << 8) |
                 ((uint32_t)byte[4 * w + 2] << 16) | ((uint32_t)byte[4 * w + 3] << 24);
    }
    return FECKLESS_UMODE_OK;
}

void feckless_umode_ctx_init(feckless_umode_ctx_t *ctx, uint32_t pc,
                             uint32_t stack_lo, uint32_t stack_hi, uint32_t a0)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->pc = pc;
    ctx->x[2] = stack_hi;       /* sp; the guard's upper bound is inclusive */
    ctx->x[10] = a0;
    ctx->u_sp_min = stack_lo;
    ctx->u_sp_max = stack_hi;
}
