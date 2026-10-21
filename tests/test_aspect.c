/* games/totk/runtime/aspect_math.h without the game: TOTK_ASPECT parsing, the Vert+ factor, the UI squeeze, the camera
   site pattern on 1.4.3's own words, and (on Apple silicon) the stubs run for real against a fake projection. */
#include "../games/totk/runtime/aspect_math.h"
#include <math.h>
#include <stdlib.h>
#if defined(__aarch64__) && defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <sys/mman.h>
#endif

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(double a, double b) { return fabs(a - b) < 1e-4; }

static void parsing(void) {
    CHECK(aspect_parse(NULL) == 0 && aspect_parse("") == 0 && aspect_parse("0") == 0, "unset");
    CHECK(near(aspect_parse("16:10"), 1.6), "16:10");
    CHECK(near(aspect_parse("2560x1600"), 1.6), "2560x1600");
    CHECK(near(aspect_parse("1.6"), 1.6), "1.6");
    CHECK(near(aspect_parse("21:9"), 21.0 / 9), "21:9");
    CHECK(near(aspect_parse("3440x1440"), 3440.0 / 1440), "3440x1440");
    CHECK(aspect_parse("display") == ASPECT_DISPLAY, "display");
    const char *bad[] = {"wide", "16:", "16:10x", "1.6 ", "0.4", "9", "1:3", "0:9", "-2", "16:0", "Display"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(aspect_parse(bad[i]) == ASPECT_BAD, "\"%s\" refused", bad[i]);
}

static void factors(void) {
    CHECK(aspect_effective(16.0 / 9) == 0, "16:9 is the game's own");
    CHECK(aspect_effective(1920.0 / 1088) == 0, "within 1%% of 16:9");
    CHECK(aspect_effective(0) == 0 && aspect_effective(ASPECT_DISPLAY) == 0, "unset");
    CHECK(near(aspect_effective(1.6), 1.6), "16:10 kept");
    CHECK(!aspect_vert_wanted(NULL) && !aspect_vert_wanted("") && !aspect_vert_wanted("0") && !aspect_vert_wanted("yes"), "Vert+ off by default");
    CHECK(aspect_vert_wanted("1"), "TOTK_ASPECT_VERT=1 turns it on");
    CHECK(aspect_vert_factor(0) == 0 && aspect_vert_factor((float)(21.0 / 9)) == 0, "no Vert+ at 16:9 and wider");
    CHECK(near(aspect_vert_factor(1.6f), 10.0 / 9), "16:10: %f", aspect_vert_factor(1.6f));
    CHECK(near(aspect_vert_factor(4.0f / 3), 4.0 / 3), "4:3");
    double ux, uy;
    aspect_ui_squeeze(1.6, &ux, &uy);
    CHECK(ux == 1 && near(uy, 10.0 / 9), "16:10 flattens: %f %f", ux, uy);
    aspect_ui_squeeze(21.0 / 9, &ux, &uy);
    CHECK(near(ux, 21.0 / 16) && uy == 1, "21:9 narrows: %f %f", ux, uy);
    aspect_ui_squeeze(32.0 / 9, &ux, &uy);
    CHECK(near(ux, 2) && uy == 1, "32:9 narrows: %f %f", ux, uy);
    aspect_ui_squeeze(3440.0 / 1440, &ux, &uy);   /* the same on a canvas that is itself that wide (TOTK_OUTPUT_RES=3440x1440) */
    CHECK(near(ux, 3440.0 / 1440 * 9 / 16) && uy == 1, "3440x1440: %f %f", ux, uy);
    aspect_ui_squeeze(0, &ux, &uy);
    CHECK(ux == 1 && uy == 1, "off");
    aspect_ui_squeeze(16.0 / 9, &ux, &uy);
    CHECK(ux == 1 && uy == 1, "16:9");
    /* the HUD band at 16:10: a full-canvas viewport flattened about the centre covers 2560x1440 of a 2560x1600 window */
    aspect_ui_squeeze(1.6, &ux, &uy);
    double vy = 540 + (0 - 540) / uy, vh = 1080 / uy;
    CHECK(near(vy / 1080 * 1600, 80) && near(vh / 1080 * 1600, 1440), "band %f %f", vy / 1080 * 1600, vh / 1080 * 1600);
}

static void sites(void) {
    /* main+0x628c74 and +0xb43460 (1.4.3 camera update and setup), site-16 .. site+12 */
    const uint32_t cam1[8] = {0x94963a03, 0xbd00ae80, 0x91051268, 0x39002295, 0xbd414260, 0x39002295, 0xbd00b280, 0xf9400108};
    const uint32_t cam2[8] = {0x1e204100, 0x9481d007, 0xbd00aee0, 0x390022f8, 0xbd406ac0, 0x390022f8, 0xbd00b2e0, 0xf846c2c8};
    /* main+0x1ff04f4, a viewport site: the ratio only, no setFovy before it */
    const uint32_t vp[8] = {0x1e243800, 0x1e223821, 0x2d060804, 0x39002109, 0x1e211800, 0xbd00b100, 0xb9401a68, 0x52b00009};
    CHECK(aspect_vert_reg(cam1) == 20, "camera update: x%d", aspect_vert_reg(cam1));
    CHECK(aspect_vert_reg(cam2) == 23, "camera setup: x%d", aspect_vert_reg(cam2));
    CHECK(aspect_vert_reg(vp) == -1, "viewport site has no Vert+");
    CHECK(aspect_site_ok(cam1[4]) && aspect_site_ok(cam2[4]) && aspect_site_ok(vp[4]), "site instructions");
    uint32_t odd[8]; memcpy(odd, cam1, sizeof odd);
    odd[6] = 0xbd00b140;   /* str s0, [x10, #0xb0]: the stub's own register */
    odd[1] = 0xbd00ad40;   /* str s0, [x10, #0xac] */
    CHECK(aspect_vert_reg(odd) == -1, "x10 refused");
    memcpy(odd, cam1, sizeof odd); odd[4] = 0xbd414270; odd[6] = 0xbd00b290;   /* s16: a scratch register */
    CHECK(aspect_vert_reg(odd) == -1, "s16 refused");
    /* site-64 .. site-4: the camera's fovy load (ldr s0, [x19, #0x13c] / [x22, #0x64]), fmul, str s0, [x<p>, #0xa0] */
    const uint32_t f1[16] = {0x39002295, 0xbd009e80, 0xbd413e60, 0x1e210808, 0xbd00a280, 0x1e204100, 0x949639fd, 0xbd00a680,
                             0x1e204100, 0x949639ee, 0xbd00aa80, 0x1e204100, 0x94963a03, 0xbd00ae80, 0x91051268, 0x39002295};
    const uint32_t f2[16] = {0xbd4062c0, 0x390022f8, 0xbd009ee0, 0xbd4066c0, 0x1e210808, 0xbd00a2e0, 0x1e204100, 0x9481d001,
                             0xbd00a6e0, 0x1e204100, 0x9481cff2, 0xbd00aae0, 0x1e204100, 0x9481d007, 0xbd00aee0, 0x390022f8};
    const uint32_t f3[16] = {0x927ef529, 0xbd400122, 0x927ef549, 0xbd400123, 0x52800029, 0x1e222000, 0x1e224c04, 0x1e204c40,
                             0x1e232020, 0x1e234c22, 0x1e214c61, 0x2d070400, 0x1e243800, 0x1e223821, 0x2d060804, 0x39002109};
    CHECK(aspect_fovy_at(f1, 20) == 2, "camera update fovy load: %d", aspect_fovy_at(f1, 20));
    CHECK(aspect_fovy_at(f2, 23) == 3, "camera setup fovy load: %d", aspect_fovy_at(f2, 23));
    CHECK(aspect_fovy_at(f1, 23) == -1 && aspect_fovy_at(f3, 8) == -1, "no fovy load for another register or a viewport site");
    uint32_t w[74];
    CHECK(aspect_stub(cam1[4], 0x1000, 0x1000 + (1 << 28), 0x4000, w) != NULL, "branch out of range refused");
    CHECK(aspect_vstub(f1[2], 0x1000, 0x2000, 0x1000 + (1ULL << 33), w) != NULL, "data page out of range refused");
}

#if defined(__aarch64__) && defined(__APPLE__)
/* Runs the stub at `site` with x1 = the value its instruction loads (ldr s0, [x1]); x2, x3, x8, x10, x11, x15, x17,
   s1, s16-s18 and the flags are set before and read after. out: those 7 x registers, s1, s16-s18, s0, nzcv. */
static void call(const void *site, const float *value, uint64_t out[13]) {
    uint32_t f[5];
    __asm__ volatile(
        "mov x1, %[value]\n"
        "mov x2, #2\n mov x3, #3\n mov x8, #8\n mov x10, #1234\n mov x11, #5678\n mov x15, #15\n mov x17, #17\n"
        "fmov s1, #5.0\n fmov s16, #2.0\n fmov s17, #3.0\n fmov s18, #4.0\n"
        "mov x9, #0x60000000\n msr nzcv, x9\n"
        "blr %[fn]\n"
        "mrs x9, nzcv\n str x9, [%[o], #96]\n"
        "stp x2, x3, [%[o]]\n stp x8, x10, [%[o], #16]\n stp x11, x15, [%[o], #32]\n str x17, [%[o], #48]\n"
        "str s1, [%[f]]\n str s16, [%[f], #4]\n str s17, [%[f], #8]\n str s18, [%[f], #12]\n str s0, [%[f], #16]\n"
        : : [value] "r"(value), [fn] "r"(site), [o] "r"(out), [f] "r"(f)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30",
          "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26",
          "v27", "v28", "v29", "v30", "v31", "memory", "cc");
    for (int i = 0; i < 5; i++) out[7 + i] = f[i];
}
static float fl(uint64_t bits) { uint32_t b = (uint32_t)bits; float v; memcpy(&v, &b, 4); return v; }
static void kept(const uint64_t o[13], const char *what) {
    static const uint64_t x[7] = {2, 3, 8, 1234, 5678, 15, 17};
    for (int i = 0; i < 7; i++) CHECK(o[i] == x[i], "%s: x register %d kept (%llu)", what, i, (unsigned long long)o[i]);
    CHECK(fl(o[7]) == 5 && fl(o[8]) == 2 && fl(o[9]) == 3 && fl(o[10]) == 4, "%s: s1, s16-s18 kept", what);
    CHECK(o[12] == 0x60000000, "%s: flags kept (%#llx)", what, (unsigned long long)o[12]);
}

static void run(void) {
    uint8_t *mem = mmap(NULL, 0x8000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (mem == MAP_FAILED) { printf("skip: no MAP_JIT memory\n"); return; }
    const uint32_t ins = 0xbd400020;   /* ldr s0, [x1] */
    const uint64_t site = 0x100, plain = 0x1000, vert = 0x1100, data = 0x4000;
    uint32_t s[74] = {0};
    pthread_jit_write_protect_np(0);
    memset(mem, 0, 0x8000);
    const char *e1 = aspect_vstub(ins, vert, site, data, s);
    memcpy(mem + vert, s, ASPECT_VSTUB < sizeof s ? ASPECT_VSTUB : sizeof s);
    const char *e2 = aspect_stub(ins, plain, site + 16, data, s);
    memcpy(mem + plain, s, ASPECT_STUB);
    uint32_t j1[2] = {aspect_b(site, vert), 0xd65f03c0}, j2[2] = {aspect_b(site + 16, plain), 0xd65f03c0};   /* b <stub>; ret */
    memcpy(mem + site, j1, 8); memcpy(mem + site + 16, j2, 8);
    float (*tf)(float) = tanf, (*af)(float) = atanf;
    memcpy(mem + data + ASPECT_TANF_SLOT, &tf, 8); memcpy(mem + data + ASPECT_ATANF_SLOT, &af, 8);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(mem, 0x8000);
    CHECK(!e1 && !e2 && j1[0] && j2[0], "stubs built");
    float *slot = (float *)(void *)(mem + data + ASPECT_DATA_SLOT), *kslot = (float *)(void *)(mem + data + ASPECT_VERT_SLOT);
    const float game = 16.0f / 9, fovy = 0.8f;
    for (int c = 0; c < 3; c++) {
        float ratio = c ? 1.6f : 0, k = c == 2 ? aspect_vert_factor(1.6f) : 0;
        pthread_jit_write_protect_np(0); *slot = ratio; *kslot = k; pthread_jit_write_protect_np(1);
        uint64_t o[13];
        call(mem + site + 16, &game, o);   /* the aspect site */
        kept(o, "plain stub");
        CHECK(fl(o[11]) == (ratio ? ratio : game), "case %d: ratio %f", c, fl(o[11]));
        call(mem + site, &fovy, o);        /* the fovy load */
        kept(o, "Vert+ stub");
        float want = k ? 2 * atanf(k * tanf(fovy / 2)) : fovy;
        CHECK(near(fl(o[11]), want), "case %d: fovy %f want %f", c, fl(o[11]), want);
        if (k) {   /* the 16:9 horizontal view kept: ratio * tan(fovy' / 2) equals 16/9 * tan(fovy / 2) */
            CHECK(near(1.6 * tan(fl(o[11]) / 2), game * tan(fovy / 2)), "Vert+ keeps the horizontal view");
            printf("16:10 Vert+: vertical view %.1f -> %.1f degrees\n", fovy * 180 / M_PI, fl(o[11]) * 180 / M_PI);
        }
    }
    munmap(mem, 0x8000);
    printf("stubs ran\n");
}
#endif

int main(void) {
    parsing();
    factors();
    sites();
#if defined(__aarch64__) && defined(__APPLE__)
    run();
#else
    printf("skip: the stubs only run on Apple silicon\n");
#endif
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("aspect ok\n");
    return 0;
}
