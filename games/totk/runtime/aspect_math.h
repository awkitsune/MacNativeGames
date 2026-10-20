/* The arithmetic and machine code of aspect_patch.h, without the runtime, so tests/test_aspect.c can check it: TOTK_ASPECT
   parsing, the Vert+ factor for ratios below 16:9, and the stubs put behind the camera sites.
   Vert+: the game keeps its vertical field of view and the aspect ratio decides how much is seen at the sides, so a ratio
   below 16:9 (a MacBook's 16:10) would show less at the sides. sead's perspective projection keeps fovy (+0xa0) and the
   sine, cosine and tangent of half of it (+0xa4, +0xa8, +0xac) beside the aspect ratio (+0xb0); 1.4.3 inlines its
   setFovy in both camera sites, just before the aspect ratio that the sites patch. Multiplying the tangent by
   (16/9) / ratio (and the sine and cosine to match) keeps the 16:9 horizontal view and adds view above and below. fovy
   itself is left at the game's value. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASPECT_GAME (16.0 / 9.0)
#define ASPECT_DATA_SLOT 56   /* the ratio's float on the data page after main; 0.0 = the game's own */
#define ASPECT_VERT_SLOT 60   /* the Vert+ factor beside it; 0.0 = none */
#define ASPECT_STUB 32        /* bytes per plain stub */
#define ASPECT_VSTUB 128      /* bytes per Vert+ stub */

enum { ASPECT_BAD = -1, ASPECT_DISPLAY = -2 };

/* TOTK_ASPECT as a ratio: 0 when unset or "0", ASPECT_DISPLAY for "display" (the window's shape), ASPECT_BAD when it is
   not <w>:<h>, <w>x<h> or a ratio between 0.5 and 8. */
static inline double aspect_parse(const char *e) {
    if (!e || !*e || !strcmp(e, "0")) return 0;
    if (!strcmp(e, "display")) return ASPECT_DISPLAY;
    double a = 0, w, h; char rest;
    if ((sscanf(e, "%lf:%lf%c", &w, &h, &rest) == 2 || sscanf(e, "%lfx%lf%c", &w, &h, &rest) == 2) && w > 0 && h > 0) a = w / h;
    else if (sscanf(e, "%lf%c", &w, &rest) == 1) a = w;
    return a >= 0.5 && a <= 8 ? a : ASPECT_BAD;
}

/* The ratio to put in for a wanted one: 0 (the game's own, no stretch) within 1% of 16:9 or outside 0.5..8. */
static inline float aspect_effective(double a) {
    if (!(a >= 0.5 && a <= 8) || (a / ASPECT_GAME > 0.99 && a / ASPECT_GAME < 1.01)) return 0;
    return (float)a;
}

/* The Vert+ factor for ratio a (the half-angle tangent is multiplied by it): (16/9) / a below 16:9, 0 (none) otherwise. */
static inline float aspect_vert_factor(float a) {
    return a > 0 && a < ASPECT_GAME ? (float)(ASPECT_GAME / a) : 0;
}

/* The UI squeeze for a shown ratio a (nvn_mtl.m): the game lays its UI out for 16:9, so it comes out stretched by
   a / (16/9) however wide the canvas is (a 16:9 canvas stretched at the present, or one as wide as the screen,
   TOTK_OUTPUT_RES=3440x1440). UI draws are narrowed by *ux (> 1 wider than 16:9) or flattened by *uy (> 1 below it)
   about the canvas centre. Both 1 when a is 0 or within 0.1% of 16:9. */
static inline void aspect_ui_squeeze(double a, double *ux, double *uy) {
    double f = a > 0 ? a / ASPECT_GAME : 1;
    *ux = f > 1.001 ? f : 1;
    *uy = f < 0.999 ? 1 / f : 1;
}

/* b from -> to, or 0 when out of range. */
static inline uint32_t aspect_b(uint64_t from, uint64_t to) {
    int64_t d = (int64_t)to - (int64_t)from;
    if ((d & 3) || d < -(1 << 27) || d >= (1 << 27)) return 0;
    return 0x14000000u | ((uint32_t)(d >> 2) & 0x3ffffff);
}
/* adrp x<r> at `at` for the page of `to` (offsets from a page aligned base), or 0 when out of range. */
static inline uint32_t aspect_adrp(unsigned r, uint64_t at, uint64_t to) {
    int64_t pages = (int64_t)(to >> 12) - (int64_t)(at >> 12);
    if (pages < -(1 << 20) || pages >= (1 << 20)) return 0;
    uint32_t imm = (uint32_t)pages & 0x1fffff;
    return 0x90000000u | ((imm & 3) << 29) | ((imm >> 2) << 5) | r;
}

/* Is ins one a site may hold: fdiv s<d>, ... or ldr s<d>, [x<n>, #imm]. */
static inline int aspect_site_ok(uint32_t ins) {
    return (ins & 0xffe0fc00) == 0x1e201800 || (ins & 0xffc00000) == 0xbd400000;
}
static inline int aspect_str_s(uint32_t w, unsigned off) { return (w & 0xffc00000) == 0xbd000000 && ((w >> 10) & 0xfff) * 4 == off; }

/* The projection register of a camera site: words[0..7] are site-16 .. site+12, the site at words[4]. 1.4.3:
   str s0, [x<p>, #0xac] (the tangent) shortly before, str s<d>, [x<p>, #0xb0] (the ratio the site loads) shortly after.
   -1 when the pattern is not there or the register collides with the stub's own. */
static inline int aspect_vert_reg(const uint32_t words[8]) {
    unsigned d = words[4] & 31;
    if (!aspect_site_ok(words[4]) || (d >= 16 && d <= 18)) return -1;
    for (int a = 5; a < 8; a++) {
        if (!aspect_str_s(words[a], 0xb0) || (words[a] & 31) != d) continue;
        unsigned p = (words[a] >> 5) & 31;
        if (p == 10 || p == 11 || p == 31) return -1;
        for (int b = 0; b < 4; b++) if (aspect_str_s(words[b], 0xac) && ((words[b] >> 5) & 31) == p) return (int)p;
    }
    return -1;
}

/* The plain stub (8 words) at offset s for the site's instruction ins: run it and, while a ratio sits in the data page's
   slot, put that instead. It keeps its scratch register on the stack, so no liveness is assumed. NULL or what failed. */
static inline const char *aspect_stub(uint32_t ins, uint64_t s, uint64_t site, uint64_t data_off, uint32_t out[8]) {
    uint32_t adrp = aspect_adrp(10, s + 8, data_off), back = aspect_b(s + 28, site + 4);
    if (!adrp) return "aspect patch: data page out of ADRP range";
    if (!back) return "aspect patch: branch out of range";
    uint32_t stub[8] = {
        ins,                                                              /* the game's own ratio into s<d> */
        0xf81f0fea,                                                       /* str x10, [sp, #-16]! */
        adrp,                                                             /* adrp x10, <data page> */
        0xb9400000u | ((uint32_t)(ASPECT_DATA_SLOT / 4) << 10) | (10 << 5) | 10, /* ldr w10, [x10, #slot] */
        0x3400004au,                                                      /* cbz w10, +8: off, keep it */
        0x1e270000u | (10 << 5) | (ins & 31),                             /* fmov s<d>, w10: the ratio */
        0xf84107ea,                                                       /* ldr x10, [sp], #16 */
        back};                                                            /* b <after the instruction> */
    memcpy(out, stub, sizeof stub);
    return NULL;
}

/* The Vert+ stub (28 of its 32 words) for a camera site whose projection is in x<p>: the plain stub's ratio, then, while
   the Vert+ factor k is set, tan' = k tan, cos' = 1 / sqrt(1 + tan'^2), sin' = tan' cos' over the half-angle values the
   game has just stored. x10, x11 and s16-s18 are kept on the stack; flags are not touched (cbz, no compares). */
static inline const char *aspect_vstub(uint32_t ins, unsigned p, uint64_t s, uint64_t site, uint64_t data_off, uint32_t out[32]) {
    uint32_t adrp = aspect_adrp(10, s + 20, data_off), back = aspect_b(s + 27 * 4, site + 4);
    if (!adrp) return "aspect patch: data page out of ADRP range";
    if (!back) return "aspect patch: branch out of range";
    const uint32_t d = ins & 31, P = p << 5;
#define LDS(t, off) (0xbd400000u | ((uint32_t)((off) / 4) << 10) | P | (t))
#define STS(t, off) (0xbd000000u | ((uint32_t)((off) / 4) << 10) | P | (t))
#define FOP(op, rd, rn, rm) ((op) | ((uint32_t)(rm) << 16) | ((uint32_t)(rn) << 5) | (rd))
    const uint32_t stub[32] = {
        ins,                                                    /*  0 the game's own ratio into s<d> */
        0xd10083ff,                                             /*  1 sub sp, sp, #32 */
        0xa9002fea,                                             /*  2 stp x10, x11, [sp] */
        0x2d0247f0,                                             /*  3 stp s16, s17, [sp, #16] */
        0xbd001bf2,                                             /*  4 str s18, [sp, #24] */
        adrp,                                                   /*  5 adrp x10, <data page> */
        0xb9400000u | ((ASPECT_DATA_SLOT / 4) << 10) | (10 << 5) | 11, /*  6 ldr w11, [x10, #ratio] */
        0x34000000u | ((23 - 7) << 5) | 11,                     /*  7 cbz w11, 23: off */
        0x1e270000u | (11 << 5) | d,                            /*  8 fmov s<d>, w11: the ratio */
        0xb9400000u | ((ASPECT_VERT_SLOT / 4) << 10) | (10 << 5) | 11, /*  9 ldr w11, [x10, #k] */
        0x34000000u | ((23 - 10) << 5) | 11,                    /* 10 cbz w11, 23: no Vert+ */
        0x1e270000u | (11 << 5) | 16,                           /* 11 fmov s16, w11: k */
        LDS(17, 0xac),                                          /* 12 ldr s17, [x<p>, #0xac]: tan */
        FOP(0x1e200800u, 17, 17, 16),                           /* 13 fmul s17, s17, s16: tan' */
        STS(17, 0xac),                                          /* 14 str s17, [x<p>, #0xac] */
        FOP(0x1e200800u, 18, 17, 17),                           /* 15 fmul s18, s17, s17 */
        0x1e2e1010,                                             /* 16 fmov s16, #1.0 */
        FOP(0x1e202800u, 18, 18, 16),                           /* 17 fadd s18, s18, s16 */
        0x1e21c000u | (18 << 5) | 18,                           /* 18 fsqrt s18, s18 */
        FOP(0x1e201800u, 18, 16, 18),                           /* 19 fdiv s18, s16, s18: cos' */
        STS(18, 0xa8),                                          /* 20 str s18, [x<p>, #0xa8] */
        FOP(0x1e200800u, 17, 17, 18),                           /* 21 fmul s17, s17, s18: sin' */
        STS(17, 0xa4),                                          /* 22 str s17, [x<p>, #0xa4] */
        0xbd401bf2,                                             /* 23 ldr s18, [sp, #24] */
        0x2d4247f0,                                             /* 24 ldp s16, s17, [sp, #16] */
        0xa9402fea,                                             /* 25 ldp x10, x11, [sp] */
        0x910083ff,                                             /* 26 add sp, sp, #32 */
        back};                                                  /* 27 b <after the instruction> */
#undef LDS
#undef STS
#undef FOP
    memcpy(out, stub, sizeof stub);
    return NULL;
}
