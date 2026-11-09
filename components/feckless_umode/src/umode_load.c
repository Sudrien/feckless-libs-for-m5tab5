/* SPDX-License-Identifier: MIT
 *
 * See feckless_umode_load.h.
 *
 * esp_elf allocates the app's text and data, and its own working buffers,
 * with esp_elf_malloc(). The component's link wraps it (CMakeLists.txt): while
 * a box is being loaded, every allocation comes from the box, so the program
 * lands where the grant is, and everything else -- the trusted path, a
 * launcher's own loads -- goes to the heap as before. What esp_elf keeps after
 * the load (the symbol names) is in the box too, where the program could
 * rewrite it; it is only ever freed, and a free into the box is refused unless
 * it is exactly an allocation.
 *
 * esp_elf_request() is not used: it calls the entry, and the constructors,
 * in M-mode. esp_elf_deinit() runs the fini functions in M-mode, so they are
 * cleared first.
 */
#include "feckless_umode_load.h"

#include <errno.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "private/elf_symbol.h"     /* u-feckless-0014: elf_set_symbol_resolver() */
#include "private/elf_types.h"

#include "feckless_umode.h"          /* FECKLESS_UMODE_MAX_GRANTS, _R, _W */
#include "feckless_umode_box.h"
#include "feckless_umode_idf.h"
#include "feckless_umode_pmp.h"

static const char *TAG = "feckless_umode";

#define MAX_CTORS 64
#define BOX_ALIGN 4096u

struct feckless_umode_app {
    feckless_box_t box;
    feckless_umode_window_t win;
    feckless_umode_calls_t calls;
    esp_elf_t elf;
    struct esp_elfsym *syms;        /* host memory: the names point at calls[] */
    void *block;
    uintptr_t exit_stub;
    bool relocated;
    feckless_umode_grant_t grants[FECKLESS_UMODE_MAX_GRANTS];   /* [0] is the box */
    int ngrants;
};

/* The box being loaded or unloaded, or NULL: the wrap's switch. */
static feckless_box_t *s_loading;
static feckless_umode_app_t *s_loaded;

void *__real_esp_elf_malloc(uint32_t n, bool exec);
void __real_esp_elf_free(void *ptr);

void *__wrap_esp_elf_malloc(uint32_t n, bool exec)
{
    if (s_loading) {
        return (void *)feckless_box_alloc(s_loading, n);
    }
    return __real_esp_elf_malloc(n, exec);
}

void __wrap_esp_elf_free(void *ptr)
{
    const uintptr_t p = (uintptr_t)ptr;
    if (s_loaded && p >= s_loaded->box.base && p < s_loaded->box.base + s_loaded->box.size) {
        feckless_box_free(&s_loaded->box, p);   /* refused unless it is an allocation */
        return;
    }
    if (s_loading && p >= s_loading->base && p < s_loading->base + s_loading->size) {
        feckless_box_free(s_loading, p);
        return;
    }
    __real_esp_elf_free(ptr);
}

/*
 * u-feckless-0014: the names a box's ELF is relocated against are the box's
 * table and nothing else.
 *
 * esp_elf resolves an import with elf_find_sym(), whose default searches
 * elf_loader's own tables first -- with CONFIG_ELF_LOADER_LIBC_SYMBOLS, its
 * default, malloc, calloc, realloc, free, puts, putchar, printf, __divdf3 and
 * more, as the host's functions -- and only then the tables registered with
 * esp_elf_register_symbol(). On a Tab5 the box's table was registered and
 * Spurious's calloc still resolved to IDF's: `instruction access fault at
 * 0x4ff09c62` (calloc, heap.c), stopped by the PMP. So the box's table is not
 * registered at all; while a box is relocated, this is the resolver. A name
 * not here returns 0, and esp_elf fails the load naming it.
 */
static struct esp_elfsym *s_resolving;
static const feckless_umode_call_t *s_resolving_calls;

/* u-feckless-0021: a data entry is copied into the box the first time the
 * ELF names it, not at every load -- Ark12's tables are 380 KB, and only an
 * app that draws its own text reads them. Allocated from the box as the
 * relocation's own allocations are (s_loading). */
static uintptr_t box_resolve(const char *name)
{
    for (struct esp_elfsym *p = s_resolving; p && p->name; p++) {
        if (strcmp(p->name, name) != 0) {
            continue;
        }
        if (!p->sym) {
            const feckless_umode_call_t *c = &s_resolving_calls[p - s_resolving];
            const uint32_t len = c->data_len ? c->data_len : 4;
            const uintptr_t d = s_loading ? feckless_box_alloc(s_loading, len) : 0;
            if (!d) {
                ESP_LOGE(TAG, "no room in the box for %s (%u bytes)", name, (unsigned)len);
                return 0;
            }
            memset((void *)d, 0, len);
            if (c->data && c->data_len) {
                memcpy((void *)d, c->data, c->data_len);
            }
            p->sym = (const void *)d;
        }
        return (uintptr_t)p->sym;
    }
    return 0;
}

static bool in_box(const feckless_umode_app_t *a, uintptr_t p)
{
    return p >= a->box.base && p < a->box.base + a->box.size;
}

static void free_app(feckless_umode_app_t *a)
{
    if (a->relocated) {
        a->elf.fini_count = 0;                  /* never in M-mode */
        a->elf.fini_array = NULL;
        s_loading = &a->box;
        esp_elf_deinit(&a->elf);
        s_loading = NULL;
    }
    heap_caps_free(a->syms);
    heap_caps_free(a->block);
    heap_caps_free(a);
}

esp_err_t feckless_umode_load(const uint8_t *image, size_t size,
                              const feckless_umode_call_t *calls, uint32_t n,
                              uint32_t box_size, uint32_t stack,
                              feckless_umode_app_t **out)
{
    *out = NULL;
    if (s_loaded || s_loading) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!image || (!calls && n) || n > FECKLESS_BOX_MAX_STUBS) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The app's bookkeeping is host memory, internal RAM: never the box. */
    feckless_umode_app_t *a = heap_caps_calloc(1, sizeof *a, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    /* u-feckless-0011: 4 KB at both ends, whatever the PMP's granularity
     * turns out to be up to that (umode_pmp.c logs it). */
    box_size &= ~(uint32_t)(BOX_ALIGN - 1);
    a->block = heap_caps_aligned_alloc(BOX_ALIGN, box_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a->block || !feckless_box_init(&a->box, (uintptr_t)a->block, box_size, stack)) {
        ESP_LOGE(TAG, "no box of %u KB with %u KB of stack", (unsigned)(box_size >> 10), (unsigned)(stack >> 10));
        goto fail;
    }

    /* The stubs first; the symbol table points at them, and at each data
     * entry once box_resolve() has copied it (u-feckless-0021). */
    const uintptr_t stubs = feckless_box_alloc(&a->box, FECKLESS_BOX_STUBS_BYTES(n));
    a->syms = heap_caps_calloc(n + 1, sizeof *a->syms, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!stubs || !a->syms) {
        goto fail;
    }
    a->exit_stub = feckless_box_write_stubs(stubs, n);
    for (uint32_t i = 0; i < n; i++) {
        a->syms[i].name = calls[i].name;
        if (calls[i].fn) {
            a->syms[i].sym = (const void *)(stubs + i * FECKLESS_BOX_STUB_SIZE);
        } else {
            a->syms[i].sym = NULL;              /* copied on first use, by box_resolve() */
        }
    }
    /* syms[n] is the terminator, zeroed by calloc. */

    /* Relocate, every allocation into the box and every name from the box's
     * table alone. The relocation's own cache flush (a full write-back and
     * I-cache invalidate on the P4) covers the stubs written above. */
    esp_elf_init(&a->elf);
    s_loading = &a->box;
    s_resolving = a->syms;
    s_resolving_calls = calls;
    elf_set_symbol_resolver(box_resolve);
    int r = esp_elf_relocate(&a->elf, image, size);
    elf_reset_symbol_resolver();
    s_resolving = NULL;
    s_resolving_calls = NULL;
    s_loading = NULL;
    if (r < 0) {
        ESP_LOGE(TAG, "esp_elf_relocate: %d (an import not in the box's table is named above)", r);
        err = r == -ENOMEM ? ESP_ERR_NO_MEM : ESP_ERR_NOT_FOUND;
        goto fail;
    }
    a->relocated = true;
    if (!a->elf.entry || !in_box(a, (uintptr_t)a->elf.entry)) {
        ESP_LOGE(TAG, "the entry is not in the box");
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

    /* The grant: the box, and nothing else (until feckless_umode_app_grant()). */
    a->grants[0] = (feckless_umode_grant_t){
        (uint32_t)a->box.base, (uint32_t)(a->box.base + a->box.size),
        FECKLESS_UMODE_R | FECKLESS_UMODE_W | FECKLESS_UMODE_X,
    };
    a->ngrants = 1;
    if (feckless_umode_pmp_apply(a->grants, a->ngrants) != ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

    a->calls.calls = calls;
    a->calls.n = n;
    a->calls.box = &a->box;
    a->win.lo = a->box.base;
    a->win.hi = a->box.base + a->box.size;
    a->win.ext = feckless_umode_calls_ext;
    a->win.ext_arg = &a->calls;
    s_loaded = a;
    *out = a;
    ESP_LOGI(TAG, "app in its box at %p, %u KB, %u calls; %u KB used by the load",
             a->block, (unsigned)(box_size >> 10), (unsigned)n,
             (unsigned)((a->box.n ? a->box.a[a->box.n - 1].off + a->box.a[a->box.n - 1].len : 0) >> 10));
    return ESP_OK;

fail:
    free_app(a);
    return err;
}

/* One function, in a fresh context on the box's stack, returning to the exit stub. */
static feckless_umode_end_t run_one(feckless_umode_app_t *a, uintptr_t fn)
{
    uint32_t lo, hi;
    feckless_box_stack(&a->box, &lo, &hi);
    feckless_umode_ctx_init(&a->win.ctx, (uint32_t)fn, lo, hi, 0);
    a->win.ctx.x[1] = (uint32_t)a->exit_stub;   /* ra */
    return feckless_umode_run_default(&a->win);
}

feckless_umode_end_t feckless_umode_app_run(feckless_umode_app_t *a)
{
    feckless_umode_end_t end = { .reason = FECKLESS_UMODE_END_REFUSED };
    if (!a || a != s_loaded) {
        return end;
    }
    /* The arrays are in the box, where the program could rewrite them while
     * the first constructor runs, so they are read once, here. A pointer
     * outside the box would only fault, but it is not worth a window. */
    uintptr_t ctors[MAX_CTORS];
    uint32_t nc = 0;
    void (**const lists[2])(void) = { a->elf.preinit_array, a->elf.init_array };
    const uint32_t counts[2] = { a->elf.preinit_count, a->elf.init_count };
    for (int l = 0; l < 2; l++) {
        for (uint32_t i = 0; lists[l] && i < counts[l]; i++) {
            const uint32_t k = (l == 1 && a->elf.init_reverse) ? counts[l] - 1 - i : i;
            const uintptr_t f = (uintptr_t)lists[l][k];
            if (nc == MAX_CTORS || !in_box(a, f)) {
                ESP_LOGE(TAG, "constructor %u: %s", (unsigned)nc, nc == MAX_CTORS ? "too many" : "not in the box");
                return end;
            }
            ctors[nc++] = f;
        }
    }
    a->elf.init_done = true;
    for (uint32_t i = 0; i < nc; i++) {
        end = run_one(a, ctors[i]);
        if (end.reason != FECKLESS_UMODE_END_EXIT) {
            return end;
        }
    }
    return run_one(a, (uintptr_t)a->elf.entry);
}

feckless_umode_app_t *feckless_umode_app_loaded(void)
{
    return s_loaded;
}

/* u-feckless-0022 */
esp_err_t feckless_umode_app_grant(feckless_umode_app_t *a, uint32_t lo, uint32_t hi, uint32_t perm)
{
    if (!a || a != s_loaded) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((perm & FECKLESS_UMODE_X) || !(perm & FECKLESS_UMODE_R) ||
        (perm & ~(FECKLESS_UMODE_R | FECKLESS_UMODE_W)) || lo >= hi) {
        return ESP_ERR_INVALID_ARG;             /* data only: nothing outside the box runs */
    }
    for (int i = 0; i < a->ngrants; i++) {
        const feckless_umode_grant_t *g = &a->grants[i];
        if (g->lo == lo && g->hi == hi && g->perm == perm) {
            return ESP_OK;                      /* already */
        }
        if (lo < g->hi && g->lo < hi) {
            return ESP_ERR_INVALID_ARG;         /* overlapping grants: the earlier would win */
        }
    }
    if (a->ngrants == FECKLESS_UMODE_MAX_GRANTS) {
        return ESP_ERR_NO_MEM;
    }
    a->grants[a->ngrants] = (feckless_umode_grant_t){ lo, hi, perm };
    esp_err_t e = feckless_umode_pmp_apply(a->grants, a->ngrants + 1);
    if (e != ESP_OK) {
        feckless_umode_pmp_apply(a->grants, a->ngrants);   /* back to what it was */
        return e;
    }
    a->ngrants++;
    ESP_LOGI(TAG, "granted 0x%08x..0x%08x (%s) beside the box", (unsigned)lo, (unsigned)hi,
             perm & FECKLESS_UMODE_W ? "read, write" : "read");
    return ESP_OK;
}

feckless_umode_window_t *feckless_umode_app_window(feckless_umode_app_t *a)
{
    return &a->win;
}

void feckless_umode_unload(feckless_umode_app_t *a)
{
    if (!a || a != s_loaded) {
        return;
    }
    feckless_umode_pmp_apply(NULL, 0);          /* U-mode is granted nothing again */
    s_loaded = NULL;
    free_app(a);
}
