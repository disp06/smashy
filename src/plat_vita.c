#include "plat.h"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/apputil.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int _newlib_heap_size_user = 96 * 1024 * 1024;

static SceKernelLwMutexWork mtx;
static void (*mix_cb)(int16_t *, int);
static int prev_touch_n;
static uint32_t prev_held;

int plat_init(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceKernelCreateLwMutex(&mtx, "mix", 0, 0, NULL);
    sceIoMkdir("ux0:data/smashy", 0777);
    SceAppUtilInitParam ip; SceAppUtilBootParam bp;
    memset(&ip, 0, sizeof ip); memset(&bp, 0, sizeof bp);
    sceAppUtilInit(&ip, &bp);
    return 0;
}

static float axis(int v) {
    float f = (v - 128) / 127.0f;
    if (fabsf(f) < 0.18f) return 0;
    return clampf((f - signf(f) * 0.18f) / 0.82f, -1, 1);
}

void plat_poll(Input *in) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t h = 0;
    if (pad.buttons & SCE_CTRL_UP) h |= BTN_UP;
    if (pad.buttons & SCE_CTRL_DOWN) h |= BTN_DOWN;
    if (pad.buttons & SCE_CTRL_LEFT) h |= BTN_LEFT;
    if (pad.buttons & SCE_CTRL_RIGHT) h |= BTN_RIGHT;
    if (pad.buttons & SCE_CTRL_CROSS) h |= BTN_CROSS;
    if (pad.buttons & SCE_CTRL_CIRCLE) h |= BTN_CIRCLE;
    if (pad.buttons & SCE_CTRL_SQUARE) h |= BTN_SQUARE;
    if (pad.buttons & SCE_CTRL_TRIANGLE) h |= BTN_TRIANGLE;
    if (pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) h |= BTN_L;
    if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) h |= BTN_R;
    if (pad.buttons & SCE_CTRL_START) h |= BTN_START;
    if (pad.buttons & SCE_CTRL_SELECT) h |= BTN_SELECT;
    in->pressed = h & ~prev_held;
    in->held = h;
    prev_held = h;
    in->lx = axis(pad.lx);
    in->ly = axis(pad.ly);
    SceTouchData td;
    memset(&td, 0, sizeof td);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    in->touch_n = td.reportNum > 4 ? 4 : td.reportNum;
    for (int i = 0; i < in->touch_n; i++) {
        in->tx[i] = td.report[i].x * 0.5f;
        in->ty[i] = td.report[i].y * 0.5f;
    }
    in->touch_tap = (in->touch_n > prev_touch_n);
    if (in->touch_tap) { in->tap_x = in->tx[in->touch_n - 1]; in->tap_y = in->ty[in->touch_n - 1]; }
    prev_touch_n = in->touch_n;
}

double plat_time(void) {
    SceRtcTick t;
    sceRtcGetCurrentTick(&t);
    return (double)t.tick / 1000000.0;
}

const char *plat_data_dir(void) { return "ux0:data/smashy/"; }
const char *plat_save_path(void) { return "ux0:data/smashy/save.dat"; }

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    const int frames = 512;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, frames, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return 0;
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    static int16_t buf[2][512 * 2];
    int b = 0;
    for (;;) {
        mix_cb(buf[b], frames);
        sceAudioOutOutput(port, buf[b]);
        b ^= 1;
    }
    return 0;
}

void plat_audio_start(void (*mix)(int16_t *, int)) {
    mix_cb = mix;
    SceUID th = sceKernelCreateThread("audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}
void plat_lock(void) { sceKernelLockLwMutex(&mtx, 1, NULL); }
void plat_unlock(void) { sceKernelUnlockLwMutex(&mtx, 1); }
int plat_should_quit(void) { return 0; }
void plat_present_hook(int frame) { (void)frame; }

uint8_t *plat_read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 1);
    if (!d) { fclose(f); return NULL; }
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f);
    d[n] = 0;
    if (size) *size = n;
    return d;
}

void dbg_log(const char *fmt, ...) {
    FILE *f = fopen("ux0:data/smashy/log.txt", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}
