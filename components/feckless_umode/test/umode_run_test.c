/* SPDX-License-Identifier: MIT
 *
 * Host test for umode_run.c as it is: the loop and the syscalls, against a
 * scripted stand-in for feckless_umode_enter(). The stand-in plays what the
 * trap vector would have written into the context for each step.
 *
 *   make -C components/feckless_umode/test
 */
#include <stdio.h>
#include <string.h>

#include "feckless_umode_run.h"

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

#define ECALL      (0x08000000u | FUMODE_EXC_ECALL_U)    /* as the CLIC reports it: mpie set */
#define IRQ        (0x80000000u | 7u)

typedef struct {
    uint32_t cause;
    uint32_t nr, a0, a1;        /* an ecall's registers */
    uint32_t sp_fired, sp_pc;
    uint32_t mtval;
    int core;                   /* the core this window "ran" on */
} step_t;

static const step_t *script;
static int script_len, script_pos, cur_core;

static uint32_t fake_enter(feckless_umode_ctx_t *ctx)
{
    CHECK(script_pos < script_len);
    if (script_pos >= script_len) return FUMODE_EXC_ILLEGAL_INSN;      /* do not loop forever */
    const step_t *s = &script[script_pos++];
    ctx->x[17] = s->nr;
    ctx->x[10] = s->a0;
    ctx->x[11] = s->a1;
    ctx->sp_fired = s->sp_fired;
    ctx->sp_pc = s->sp_pc;
    ctx->mtval = s->mtval;
    ctx->mcause = s->cause;
    cur_core = s->core;
    return s->cause;
}

static uint32_t fake_core(void) { return (uint32_t)cur_core; }

static int delays, yields, writes;
static uint32_t last_delay, last_write_len;
static uintptr_t last_write_buf;
static void fake_delay(uint32_t ms) { delays++; last_delay = ms; }
static void fake_yield(void) { yields++; }
static void fake_write(const char *b, uint32_t n) { writes++; last_write_buf = (uintptr_t)b; last_write_len = n; }

static const feckless_umode_ops_t ops = { fake_enter, fake_delay, fake_yield, fake_core, fake_write };

/* A window over the fake addresses [0x1000, 0x2000): the host never
 * dereferences them in these tests, only compares. */
static feckless_umode_window_t *new_window(feckless_umode_window_t *w)
{
    memset(w, 0, sizeof *w);
    w->lo = 0x1000;
    w->hi = 0x2000;
    w->ctx.pc = 0x1100;
    delays = yields = writes = 0;
    script_pos = 0;
    cur_core = 0;
    return w;
}

static void run_script(const step_t *s, int n) { script = s; script_len = n; script_pos = 0; }

static void test_range(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    CHECK(feckless_umode_range_ok(&w, 0x1000, 0x1000));        /* all of it */
    CHECK(feckless_umode_range_ok(&w, 0x1fff, 1));
    CHECK(feckless_umode_range_ok(&w, 0x2000, 0));             /* empty, at the end */
    CHECK(!feckless_umode_range_ok(&w, 0x2000, 1));
    CHECK(!feckless_umode_range_ok(&w, 0x1fff, 2));
    CHECK(!feckless_umode_range_ok(&w, 0x0fff, 1));
    CHECK(!feckless_umode_range_ok(&w, 0x1000, 0x1001));
    CHECK(!feckless_umode_range_ok(&w, 0x2001, 0));
    CHECK(!feckless_umode_range_ok(&w, 0xfffffff0u, 0x20));    /* would wrap */
    CHECK(!feckless_umode_range_ok(&w, 0x1800, 0xffffffffu));
    w.hi = w.lo;
    CHECK(!feckless_umode_range_ok(&w, 0x1000, 0));            /* an empty window grants nothing */
}

static void test_strlen(void)
{
    static char buf[32];
    feckless_umode_window_t w;
    new_window(&w);
    w.lo = (uintptr_t)buf;
    w.hi = (uintptr_t)buf + sizeof buf;
    strcpy(buf, "hello");
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf, 31) == 5);
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf, 5) == -1);     /* NUL is the 6th byte */
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf, 6) == 5);
    memset(buf, 'x', sizeof buf);                                  /* no NUL inside the window */
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf, 1000) == -1);
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf + sizeof buf, 10) == -1);
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf - 1, 10) == -1);
    buf[31] = '\0';
    CHECK(feckless_umode_strlen(&w, (uintptr_t)buf + 31, 10) == 0);
}

static void test_cause_names(void)
{
    CHECK(strcmp(feckless_umode_cause_name(2), "illegal instruction") == 0);
    CHECK(strcmp(feckless_umode_cause_name(0x08000000u | 7), "store access fault") == 0);   /* mpie bit ignored */
    CHECK(strcmp(feckless_umode_cause_name(ECALL), "ecall from U-mode") == 0);
    CHECK(strcmp(feckless_umode_cause_name(0xabc), "unknown exception") == 0);
}

static void test_syscalls_and_exit(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    static const step_t s[] = {
        { ECALL, FECKLESS_SYS_NOP },
        { ECALL, FECKLESS_SYS_DELAY_MS, 25 },
        { ECALL, FECKLESS_SYS_YIELD },
        { ECALL, FECKLESS_SYS_GETCORE, 0, 0, 0, 0, 0, 1 },
        { ECALL, FECKLESS_SYS_WRITE, 0x1100, 16 },
        { ECALL, FECKLESS_SYS_EXIT, 7 },
    };
    run_script(s, 6);
    feckless_umode_end_t e = feckless_umode_run(&w, &ops);
    CHECK(e.reason == FECKLESS_UMODE_END_EXIT && e.status == 7);
    CHECK(delays == 1 && last_delay == 25 && yields == 1);
    CHECK(writes == 1 && last_write_buf == 0x1100 && last_write_len == 16);
    CHECK(w.syscalls == 6);
    CHECK(w.ctx.pc == 0x1100 + 4 * 5);          /* five serviced calls stepped; the exit did not */
    CHECK(e.pc == w.ctx.pc);
    CHECK(script_pos == 6);
}

static uint32_t a0_after[8];
static int a0_n;
static uint32_t recording_enter(feckless_umode_ctx_t *ctx)
{
    if (script_pos > 0) a0_after[a0_n++] = ctx->x[10];       /* a0 as the program would see it on resume */
    return fake_enter(ctx);
}

static void test_a0(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    static const step_t s[] = {
        { ECALL, FECKLESS_SYS_GETCORE, 0, 0, 0, 0, 0, 1 },      /* -> 1 */
        { ECALL, FECKLESS_SYS_WRITE, 0x1100, 5 },                /* -> 5 */
        { ECALL, FECKLESS_SYS_WRITE, 0x3000, 5 },                /* outside -> FAULT */
        { ECALL, 99 },                                           /* -> BADNR */
        { ECALL, FECKLESS_SYS_EXT_BASE },                        /* no ext -> BADNR */
        { ECALL, FECKLESS_SYS_EXIT },
    };
    feckless_umode_ops_t rec = ops;
    rec.enter = recording_enter;
    a0_n = 0;
    run_script(s, 6);
    feckless_umode_end_t e = feckless_umode_run(&w, &rec);
    CHECK(e.reason == FECKLESS_UMODE_END_EXIT);
    CHECK(a0_n == 5);
    CHECK(a0_after[0] == 1);
    CHECK(a0_after[1] == 5);
    CHECK(a0_after[2] == FECKLESS_SYS_ERR_FAULT);
    CHECK(a0_after[3] == FECKLESS_SYS_ERR_BADNR);
    CHECK(a0_after[4] == FECKLESS_SYS_ERR_BADNR);
    CHECK(writes == 1);                                          /* the rejected write never reached the console */
    CHECK(w.ctx.pc == 0x1100 + 4 * 5);                           /* a rejected call is still stepped past: recoverable */
}

static int ext_calls;
static bool ext_ok(feckless_umode_window_t *w, uint32_t nr, const uint32_t args[7], uint32_t *ret)
{
    (void)w;
    ext_calls++;
    if (nr == FECKLESS_SYS_EXT_BASE + 1) return false;           /* retire */
    *ret = args[0] + args[1];
    return true;
}

static void test_ext(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    w.ext = ext_ok;
    static const step_t s[] = {
        { ECALL, FECKLESS_SYS_EXT_BASE, 20, 22 },
        { ECALL, FECKLESS_SYS_EXT_BASE + 1 },
        { ECALL, FECKLESS_SYS_EXIT },        /* never reached */
    };
    feckless_umode_ops_t rec = ops;
    rec.enter = recording_enter;
    a0_n = 0;
    ext_calls = 0;
    run_script(s, 3);
    feckless_umode_end_t e = feckless_umode_run(&w, &rec);
    CHECK(e.reason == FECKLESS_UMODE_END_KILLED);
    CHECK(ext_calls == 2 && a0_after[0] == 42);
    CHECK(script_pos == 2);
}

static void test_interrupts_and_core_moves(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    static const step_t s[] = {
        { IRQ },
        { IRQ, .core = 1 },                         /* moved */
        { ECALL, FECKLESS_SYS_NOP, 0, 0, 0, 0, 0, 1 },
        { ECALL, FECKLESS_SYS_NOP, 0, 0, 0, 0, 0, 0 },   /* moved back */
        { ECALL, FECKLESS_SYS_EXIT, 0, 0, 0, 0, 0, 0 },
    };
    run_script(s, 5);
    feckless_umode_end_t e = feckless_umode_run(&w, &ops);
    CHECK(e.reason == FECKLESS_UMODE_END_EXIT);
    CHECK(w.irqs == 2);
    CHECK(w.migrations == 2);
    CHECK(w.ctx.pc == 0x1100 + 8);                  /* an interrupt does not step pc */
}

static void test_faults(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    static const step_t s[] = {
        { 0x08000000u | FUMODE_EXC_ILLEGAL_INSN, 0, 0, 0, 0, 0, 0xc0001073 },    /* a csr instruction */
        { ECALL, FECKLESS_SYS_EXIT },
    };
    run_script(s, 2);
    feckless_umode_end_t e = feckless_umode_run(&w, &ops);
    CHECK(e.reason == FECKLESS_UMODE_END_FAULT);
    CHECK((e.mcause & 0xfff) == FUMODE_EXC_ILLEGAL_INSN && e.mtval == 0xc0001073);
    CHECK(e.pc == 0x1100);
    CHECK(script_pos == 1);                         /* not resumed */
}

static void test_stack_verdict_first(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    static const step_t s[] = {
        { ECALL, FECKLESS_SYS_DELAY_MS, 5, 0, 1, 0x1234 },     /* an ordinary syscall, and the guard fired */
        { ECALL, FECKLESS_SYS_EXIT },
    };
    run_script(s, 2);
    feckless_umode_end_t e = feckless_umode_run(&w, &ops);
    CHECK(e.reason == FECKLESS_UMODE_END_STACK && e.sp_pc == 0x1234);
    CHECK(delays == 0);                             /* the call was not serviced */
    CHECK(w.syscalls == 0);
    CHECK(script_pos == 1);
}

static void test_refused(void)
{
    feckless_umode_window_t w;
    new_window(&w);
    CHECK(feckless_umode_run(NULL, &ops).reason == FECKLESS_UMODE_END_REFUSED);
    CHECK(feckless_umode_run(&w, NULL).reason == FECKLESS_UMODE_END_REFUSED);
    feckless_umode_ops_t none = ops;
    none.enter = NULL;
    CHECK(feckless_umode_run(&w, &none).reason == FECKLESS_UMODE_END_REFUSED);

    /* the window inside memory the program may write: the saved registers would be its own */
    w.lo = (uintptr_t)&w - 16;
    w.hi = (uintptr_t)&w + 16;
    run_script(NULL, 0);
    CHECK(feckless_umode_run(&w, &ops).reason == FECKLESS_UMODE_END_REFUSED);
    w.lo = (uintptr_t)&w + sizeof w;                /* just past it: fine */
    w.hi = w.lo + 64;
    static const step_t s[] = { { ECALL, FECKLESS_SYS_EXIT } };
    run_script(s, 1);
    CHECK(feckless_umode_run(&w, &ops).reason == FECKLESS_UMODE_END_EXIT);

    /* absent ops answer BADNR */
    feckless_umode_ops_t bare = { fake_enter, NULL, NULL, NULL, NULL };
    new_window(&w);
    feckless_umode_ops_t rec = bare;
    rec.enter = recording_enter;
    static const step_t t[] = { { ECALL, FECKLESS_SYS_DELAY_MS }, { ECALL, FECKLESS_SYS_WRITE, 0x1100, 1 },
                                { ECALL, FECKLESS_SYS_GETCORE }, { ECALL, FECKLESS_SYS_YIELD },
                                { ECALL, FECKLESS_SYS_EXIT } };
    a0_n = 0;
    run_script(t, 5);
    feckless_umode_run(&w, &rec);
    CHECK(a0_after[0] == FECKLESS_SYS_ERR_BADNR && a0_after[1] == FECKLESS_SYS_ERR_BADNR);
    CHECK(a0_after[2] == FECKLESS_SYS_ERR_BADNR && a0_after[3] == FECKLESS_SYS_ERR_BADNR);
}

int main(void)
{
    test_range();
    test_strlen();
    test_cause_names();
    test_syscalls_and_exit();
    test_a0();
    test_ext();
    test_interrupts_and_core_moves();
    test_faults();
    test_stack_verdict_first();
    test_refused();
    printf(failed ? "%d FAILED\n" : "umode_run: all passed\n", failed);
    return failed != 0;
}
