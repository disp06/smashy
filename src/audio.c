// 48 kHz stereo software mixer with resampling, looping voices and an IMA-ADPCM music stream
#include "audio.h"
#include "plat.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct { int rate, ch, frames, fmt; const int16_t *pcm; const uint8_t *adp; } Sound;
typedef struct {
    const Sound *s; uint32_t pos_hi; uint32_t pos_lo; uint32_t step; // 16.16 fixed position in source frames
    int vol; int loop; int active; int gen;
} Voice;

#define NV 24
static Sound sounds[S_COUNT];
static Voice voices[NV];
static int sfx_vol = 256, mus_vol = 200;
static int music_on;
// ADPCM stream state
static int ad_pred[2], ad_idx[2]; static uint32_t ad_nib; static uint32_t ad_frame; static int ad_cur[2], ad_prev[2];
static uint32_t ad_frac;
static const int idxT[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int stepT[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const char *names[S_COUNT] = {"button", "gravel", "engine", "coin", "clap", "reveal", "scrape", "splash", "circus", "rocket", "impact",
                                     "jump", "siren", "turbo", "heli", "skid", "wind", "boom", "music", "horn", "pop", "click", "beep",
                                     "btn1", "btn2", "error"};

static void ad_reset(void) { ad_pred[0] = ad_pred[1] = 0; ad_idx[0] = ad_idx[1] = 0; ad_nib = 0; ad_frame = 0; ad_cur[0] = ad_cur[1] = ad_prev[0] = ad_prev[1] = 0; ad_frac = 0; }
static int ad_decode(int k, int code) {
    int st = stepT[ad_idx[k]];
    int diff = st >> 3;
    if (code & 4) diff += st;
    if (code & 2) diff += st >> 1;
    if (code & 1) diff += st >> 2;
    if (code & 8) ad_pred[k] -= diff; else ad_pred[k] += diff;
    if (ad_pred[k] > 32767) ad_pred[k] = 32767;
    if (ad_pred[k] < -32768) ad_pred[k] = -32768;
    ad_idx[k] += idxT[code];
    if (ad_idx[k] < 0) ad_idx[k] = 0;
    if (ad_idx[k] > 88) ad_idx[k] = 88;
    return ad_pred[k];
}
static void ad_next_frame(const Sound *s) {
    if (ad_frame >= (uint32_t)s->frames) ad_reset();
    for (int k = 0; k < s->ch; k++) {
        uint8_t b = s->adp[ad_nib >> 1];
        int code = (ad_nib & 1) ? (b >> 4) : (b & 15);
        ad_nib++;
        ad_prev[k] = ad_cur[k];
        ad_cur[k] = ad_decode(k, code);
    }
    if (s->ch == 1) { ad_prev[1] = ad_prev[0]; ad_cur[1] = ad_cur[0]; }
    ad_frame++;
}

static void mix(int16_t *out, int frames) {
    static int32_t acc[2048 * 2];
    if (frames > 2048) frames = 2048;
    memset(acc, 0, sizeof(int32_t) * frames * 2);
    plat_lock();
    for (int v = 0; v < NV; v++) {
        Voice *vc = &voices[v];
        if (!vc->active) continue;
        const Sound *s = vc->s;
        int vol = vc->vol * sfx_vol >> 8;
        for (int i = 0; i < frames; i++) {
            uint32_t p = vc->pos_hi;
            if (p >= (uint32_t)s->frames) {
                if (vc->loop) { vc->pos_hi = 0; p = 0; }
                else { vc->active = 0; break; }
            }
            int l, r;
            if (s->ch == 2) { l = s->pcm[p * 2]; r = s->pcm[p * 2 + 1]; }
            else { l = r = s->pcm[p]; }
            acc[i * 2] += l * vol >> 8;
            acc[i * 2 + 1] += r * vol >> 8;
            uint32_t lo = vc->pos_lo + vc->step;
            vc->pos_hi += lo >> 16;
            vc->pos_lo = lo & 0xFFFF;
        }
    }
    const Sound *m = &sounds[S_MUSIC];
    if (music_on && m->adp) {
        uint32_t step = (uint32_t)(((uint64_t)m->rate << 16) / 48000);
        for (int i = 0; i < frames; i++) {
            ad_frac += step;
            while (ad_frac >= 0x10000) { ad_frac -= 0x10000; ad_next_frame(m); }
            int t = ad_frac;
            int l = ad_prev[0] + (((ad_cur[0] - ad_prev[0]) * t) >> 16);
            int r = ad_prev[1] + (((ad_cur[1] - ad_prev[1]) * t) >> 16);
            acc[i * 2] += l * mus_vol >> 8;
            acc[i * 2 + 1] += r * mus_vol >> 8;
        }
    }
    plat_unlock();
    for (int i = 0; i < frames * 2; i++) {
        int x = acc[i];
        out[i] = (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }
}

int audio_init(void) {
    char path[256];
    snprintf(path, sizeof path, "%ssound.pak", plat_data_dir());
    size_t sz;
    uint8_t *d = plat_read_file(path, &sz);
    if (!d || memcmp(d, "NPK1", 4)) return -1;
    uint32_t n; memcpy(&n, d + 4, 4);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 8 + i * 40;
        char nm[33]; memcpy(nm, e, 32); nm[32] = 0;
        uint32_t off, len; memcpy(&off, e + 32, 4); memcpy(&len, e + 36, 4);
        const uint8_t *b = d + off;
        for (int k = 0; k < S_COUNT; k++) if (!strcmp(nm, names[k])) {
            uint32_t h[4]; memcpy(h, b + 4, 16);
            sounds[k].rate = h[0]; sounds[k].ch = h[1]; sounds[k].frames = h[2]; sounds[k].fmt = h[3];
            if (h[3] == 1) sounds[k].adp = b + 20; else sounds[k].pcm = (const int16_t *)(b + 20);
        }
    }
    ad_reset();
    plat_audio_start(mix);
    return 0;
}

static int alloc_voice(void) {
    static int rr;
    for (int i = 0; i < NV; i++) if (!voices[i].active) return i;
    for (int i = 0; i < NV; i++) { int k = (rr + i) % NV; if (!voices[k].loop) { rr = k + 1; return k; } }
    return -1;
}

int snd_play(int id, float vol, float pitch) {
    const Sound *s = &sounds[id];
    if (!s->pcm || vol <= 0.01f) return -1;
    plat_lock();
    int v = alloc_voice();
    if (v >= 0) {
        Voice *vc = &voices[v];
        vc->s = s; vc->pos_hi = 0; vc->pos_lo = 0;
        vc->step = (uint32_t)(s->rate * pitch / 48000.0f * 65536.0f);
        vc->vol = (int)(clampf(vol, 0, 2) * 256); vc->loop = 0; vc->active = 1; vc->gen++;
    }
    plat_unlock();
    return v;
}

void snd_loop(int *voice, int id, float vol, float pitch) {
    const Sound *s = &sounds[id];
    if (!s->pcm) return;
    plat_lock();
    int v = *voice;
    if (v >= 0 && (!voices[v].active || voices[v].s != s || !voices[v].loop)) v = -1;
    if (vol <= 0.01f) {
        if (v >= 0) voices[v].active = 0;
        *voice = -1;
        plat_unlock();
        return;
    }
    if (v < 0) {
        v = alloc_voice();
        if (v < 0) { plat_unlock(); return; }
        voices[v].s = s; voices[v].pos_hi = 0; voices[v].pos_lo = 0; voices[v].loop = 1; voices[v].active = 1;
    }
    voices[v].step = (uint32_t)(s->rate * pitch / 48000.0f * 65536.0f);
    voices[v].vol = (int)(clampf(vol, 0, 2) * 256);
    *voice = v;
    plat_unlock();
}

void snd_stop_all_loops(void) {
    plat_lock();
    for (int i = 0; i < NV; i++) if (voices[i].loop) voices[i].active = 0;
    plat_unlock();
}
void snd_music(int on) { plat_lock(); music_on = on; plat_unlock(); }
void snd_set_volumes(float sfx, float music) { sfx_vol = (int)(sfx * 256); mus_vol = (int)(music * 200); }
