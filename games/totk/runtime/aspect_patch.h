/* Other picture shapes on the Mac: key 4 switches between the game's 16:9 and fitting the window / display (a 21:9 or 32:9
   monitor, a MacBook's 16:10 in full screen below the notch). TOTK_ASPECT=21:9, 32:9, 16:10, 3440x1440 or 2.39 picks a
   fixed ratio instead and starts with it on; TOTK_ASPECT=display starts on with the window's shape. Not on iPhone / iPad.
   The game keeps drawing into its 16:9 canvas, but with the camera's aspect ratio set to the other one, so the picture
   holds the other view squeezed into it; the present pass then stretches it over the window (nvn_mtl.m
   present_stretch asks runtime_aspect_now), which restores the proportions on a screen of that shape.
   The community's TOTK-Ultrawide mod for emulators (GPL; none of its code is used here) changes the camera parameter
   DefaultAspect (1.777778 in main's read-only data) and a viewport routine's width / height division. DefaultAspect is
   only copied when a camera is made, so it cannot follow a key; instead each place that hands an aspect ratio to the
   perspective projection (sead's +0xb0, found with a watchpoint on the copies) is patched where the value is produced:
   the instruction branches to a stub in main's text padding that runs it and, while a ratio sits on the data page after
   main (+56, 0 = off), puts that instead. The stub keeps its scratch register on the stack, so no liveness is assumed.
   Wider than 16:9 the vertical view stays and more is seen at the sides. Narrower (16:10), 1.4.3's two camera sites
   also widen the vertical view so the sides stay as at 16:9 (Vert+, aspect_math.h; factor on the data page +60);
   TOTK_ASPECT_VERT=0, and 1.2.1 (no room for those stubs), keep the vertical view and see less at the sides.
   The HUD, drawn into the same 16:9 canvas, is squeezed back by nvn_mtl.m (uw_ui_squeeze). Not adjusted yet: HUD
   markers that follow objects in the world and pixel-snapped positions (5 more routines TOTK-Ultrawide widens in 1.2.1)
   sit off. */
#pragma once
#include "runtime.h"
#include "aspect_math.h"
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic(uint32_t) *g_aspect_slot;   /* points into that page once the stubs are in */
static _Atomic(uint32_t) *g_aspect_vert;   /* the Vert+ factor beside it, when Vert+ stubs are in */
static float g_aspect_fixed;              /* TOTK_ASPECT's ratio; 0: the window's shape */
static atomic_int g_aspect_on;            /* key 4 */
static _Atomic(uint32_t) g_aspect_view;   /* the window's width / height (frontend.m), float bits */

/* TOTK_ASPECT as a ratio (ASPECT_DISPLAY for "display", 0 when unset or "0"). A malformed value stops the start. */
static inline double aspect_requested(void) {
    double a = aspect_parse(getenv("TOTK_ASPECT"));
    if (a == ASPECT_BAD) runtime_fail("TOTK_ASPECT must be <w>:<h>, <w>x<h>, a ratio between 0.5 and 8, or display");
    return a;
}

static inline float aspect_bits_float(uint32_t bits) { float v; memcpy(&v, &bits, 4); return v; }
static inline uint32_t aspect_float_bits(float v) { uint32_t bits; memcpy(&bits, &v, 4); return bits; }

/* The ratio in force (0 = the game's own 16:9): nvn_mtl.m stretches the picture over the window while it is set. */
float runtime_aspect_now(void) { return g_aspect_slot ? aspect_bits_float(atomic_load(g_aspect_slot)) : 0; }

/* Puts the wanted ratio (and its Vert+ factor) on the data page; logs a change. */
static void aspect_apply(void) {
    if (!g_aspect_slot) return;
    float want = !atomic_load(&g_aspect_on) ? 0 : g_aspect_fixed ? g_aspect_fixed : aspect_bits_float(atomic_load(&g_aspect_view));
    float a = aspect_effective(want), k = g_aspect_vert ? aspect_vert_factor(a) : 0;
    uint32_t old = atomic_exchange(g_aspect_slot, aspect_float_bits(a));
    if (g_aspect_vert) atomic_store(g_aspect_vert, aspect_float_bits(k));
    if (old != aspect_float_bits(a)) {
        if (a && k) fprintf(stderr, "[totk] aspect ratio %.4f, vertical view widened %.3fx (Vert+)\n", a, k);
        else if (a) fprintf(stderr, "[totk] aspect ratio %.4f\n", a);
        else fprintf(stderr, "[totk] aspect ratio 16:9 (the game's own)\n");
    }
}

/* frontend.m: the window's drawable is w x h pixels (on every change). While the shape is followed, the view follows. */
void runtime_aspect_view(double w, double h) {
    if (!(w > 0 && h > 0)) return;
    atomic_store(&g_aspect_view, aspect_float_bits((float)(w / h)));
    if (!g_aspect_fixed) aspect_apply();
}

/* Key 4 (runtime/input.c). Returns 0 where it is not available (iPhone / iPad, other game versions). */
int runtime_aspect_toggle(void) {
    if (!g_aspect_slot) return 0;
    int on = !atomic_load(&g_aspect_on); atomic_store(&g_aspect_on, on);
    aspect_apply();
    float a = runtime_aspect_now();
    if (!on) fprintf(stderr, "[totk] key 4: aspect ratio 16:9 (the game's own)\n");
    else if (a) fprintf(stderr, "[totk] key 4: aspect ratio %.4f (%s)\n", a, g_aspect_fixed ? "TOTK_ASPECT" : "fit to the window / display");
    else fprintf(stderr, "[totk] key 4: fit to the window / display, which is 16:9 already; the view follows its shape until key 4 again\n");
    return 1;
}

/* sites: instructions that leave the ratio in an s register (fdiv s<d>, ... or ldr s<d>, [x<n>, #imm]); the first ncam
   are camera sites that get a Vert+ stub (ASPECT_VSTUB bytes each, from vert_off) when vert_off is not 0 and the
   setFovy pattern is there. The other stubs go from stub_off, ASPECT_STUB bytes per site. Both areas are main's text
   padding, checked to be zero and inside the last text page; data_off is the page the loader reserves after main. */
static inline void aspect_install(Module *m, const char *version, const uint64_t *sites, unsigned nsite, unsigned ncam,
                                  uint64_t stub_off, uint64_t vert_off, uint64_t data_off) {
#if TARGET_OS_IPHONE
    (void)m; (void)version; (void)sites; (void)nsite; (void)ncam; (void)stub_off; (void)vert_off; (void)data_off;
#else
    double req = aspect_requested();
    const char *ve = getenv("TOTK_ASPECT_VERT"); int vert = vert_off && !(ve && !strcmp(ve, "0"));
    uint64_t page_end = (m->text_end + 0x3fff) & ~0x3fffULL;
    if (stub_off < m->text_end || stub_off + ASPECT_STUB * nsite > page_end) runtime_fail("aspect patch: no room in main's text padding");
    if (vert && (vert_off < m->text_end || vert_off + ASPECT_VSTUB * ncam > page_end)) runtime_fail("aspect patch: no room for the Vert+ stubs");
    if (data_off + ASPECT_VERT_SLOT + 4 > m->size) runtime_fail("aspect patch: no data page after main");
    for (unsigned i = 0; i < ASPECT_STUB * nsite; i++) if (m->base[stub_off + i]) runtime_fail("aspect patch: text padding in use");
    for (unsigned i = 0; vert && i < ASPECT_VSTUB * ncam; i++) if (m->base[vert_off + i]) runtime_fail("aspect patch: text padding for Vert+ in use");
    unsigned nvert = 0;
    for (unsigned i = 0; i < nsite; i++) {
        uint32_t ins; memcpy(&ins, m->base + sites[i], 4);
        if (!aspect_site_ok(ins)) runtime_fail("aspect patch: the camera code does not match this game version");
        int p = -1;
        if (vert && i < ncam) {
            uint32_t around[8]; memcpy(around, m->base + sites[i] - 16, sizeof around);
            if ((p = aspect_vert_reg(around)) < 0) fprintf(stderr, "[totk] aspect patch: camera site +%#llx is not the expected setFovy, no Vert+ there\n", (unsigned long long)sites[i]);
        }
        uint64_t s = p >= 0 ? vert_off + ASPECT_VSTUB * i : stub_off + ASPECT_STUB * i;
        uint32_t code[32] = {0};
        const char *err = p >= 0 ? aspect_vstub(ins, (unsigned)p, s, sites[i], data_off, code) : aspect_stub(ins, s, sites[i], data_off, code);
        uint32_t jump = aspect_b(sites[i], s);
        if (err || !jump) runtime_fail(err ? err : "aspect patch: branch out of range");
        memcpy(m->base + s, code, p >= 0 ? ASPECT_VSTUB : ASPECT_STUB);
        memcpy(m->base + sites[i], &jump, 4);
        nvert += p >= 0;
    }
    g_aspect_slot = (_Atomic(uint32_t) *)(void *)(m->base + data_off + ASPECT_DATA_SLOT);
    if (nvert) g_aspect_vert = (_Atomic(uint32_t) *)(void *)(m->base + data_off + ASPECT_VERT_SLOT);
    atomic_store(g_aspect_slot, 0);
    if (g_aspect_vert) atomic_store(g_aspect_vert, 0);
    g_aspect_fixed = req > 0 ? (float)req : 0;
    atomic_store(&g_aspect_on, req != 0);
    aspect_apply();
    char what[48]; if (g_aspect_fixed) snprintf(what, sizeof what, "%.4f (TOTK_ASPECT)", g_aspect_fixed); else snprintf(what, sizeof what, "fit to the window / display");
    fprintf(stderr, "[totk] %s aspect ratio: key 4 switches 16:9 / %s, starts %s; %u site(s), %u with Vert+%s, stubs +%#llx\n",
            version, what, req ? "on" : "at 16:9", nsite, nvert, vert_off && !vert ? " (TOTK_ASPECT_VERT=0)" : "", (unsigned long long)stub_off);
#endif
}
