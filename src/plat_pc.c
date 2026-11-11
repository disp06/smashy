// Desktop test harness: runs the game headless with scripted input and dumps screenshots.
// Script (env SMASHY_SCRIPT): lines "frame buttons lx ly"  e.g. "120 CROSS 0 0"; screenshots: env SMASHY_SHOTS="60,300,900"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

extern uint32_t *soft_framebuffer(void);
static int frame_no;
static int max_frames = 600;
typedef struct { int frame; uint32_t btn; float lx, ly; int tx, ty; } Ev;
static Ev evs[512]; static int nev;
static uint32_t prev;
static void (*mixfn)(int16_t *, int);

int plat_init(void) {
    const char *mf = getenv("SMASHY_FRAMES");
    if (mf) max_frames = atoi(mf);
    const char *sc = getenv("SMASHY_SCRIPT");
    if (sc) {
        FILE *f = fopen(sc, "r");
        char b[256];
        while (f && fgets(b, sizeof b, f) && nev < 512) {
            Ev e; memset(&e, 0, sizeof e); e.tx = -1;
            char btn[128] = "";
            if (sscanf(b, "%d %127s %f %f %d %d", &e.frame, btn, &e.lx, &e.ly, &e.tx, &e.ty) >= 2) {
                const char *names[] = {"UP", "DOWN", "LEFT", "RIGHT", "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L", "R", "START", "SELECT"};
                for (int i = 0; i < 12; i++) if (strstr(btn, names[i])) {
                    // avoid matching "R" inside other names
                    if (i == 9 && strcmp(btn, "R") != 0 && !strstr(btn, "+R") && strncmp(btn, "R+", 2) != 0) continue;
                    if (i == 8 && strcmp(btn, "L") != 0 && !strstr(btn, "+L") && strncmp(btn, "L+", 2) != 0) continue;
                    e.btn |= 1u << i;
                }
                evs[nev++] = e;
            }
        }
        if (f) fclose(f);
    }
    return 0;
}
void plat_poll(Input *in) {
    memset(in, 0, sizeof *in);
    Ev cur; memset(&cur, 0, sizeof cur); cur.tx = -1;
    for (int i = 0; i < nev; i++) if (evs[i].frame <= frame_no) cur = evs[i];
    in->held = cur.btn; in->pressed = cur.btn & ~prev; prev = cur.btn;
    in->lx = cur.lx; in->ly = cur.ly;
    if (cur.tx >= 0) { in->touch_n = 1; in->tx[0] = cur.tx; in->ty[0] = cur.ty; }
    if (mixfn) { static int16_t buf[800 * 2]; mixfn(buf, 800); }
}
double plat_time(void) { return frame_no / 60.0; }
const char *plat_data_dir(void) { const char *d = getenv("SMASHY_DATA"); return d ? d : "./data/"; }
const char *plat_save_path(void) { return "/tmp/smashy_save.dat"; }
void plat_audio_start(void (*mix)(int16_t *, int)) { mixfn = mix; }
void plat_lock(void) {}
void plat_unlock(void) {}
int plat_should_quit(void) { return frame_no >= max_frames; }
void plat_present_hook(int frame) {
    frame_no = frame + 1;
    const char *shots = getenv("SMASHY_SHOTS");
    if (!shots) return;
    char key[32]; snprintf(key, sizeof key, ",%d,", frame);
    char buf[1024]; snprintf(buf, sizeof buf, ",%s,", shots);
    if (!strstr(buf, key)) return;
    char path[256]; snprintf(path, sizeof path, "/tmp/shot_%05d.ppm", frame);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    uint32_t *fb = soft_framebuffer();
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { uint8_t c[3] = {fb[i] & 255, (fb[i] >> 8) & 255, (fb[i] >> 16) & 255}; fwrite(c, 1, 3, f); }
    fclose(f);
}
uint8_t *plat_read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 1);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f); d[n] = 0;
    if (size) *size = n;
    return d;
}
void dbg_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}
