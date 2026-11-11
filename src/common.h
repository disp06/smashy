// Smashy Road: Wanted - native PS Vita remake
// Common types and math
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define PI_F 3.14159265f

typedef struct { float x, y, z; } V3;
typedef struct { float m[16]; } M4; // column-major (GL style)

static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static inline V3 v3add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(V3 a) { return sqrtf(v3dot(a, a)); }
static inline V3 v3norm(V3 a) { float l = v3len(a); return l > 1e-6f ? v3mul(a, 1.0f / l) : a; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float signf(float v) { return v < 0 ? -1.0f : 1.0f; }
static inline float wrap_angle(float a) {
    while (a > PI_F) a -= 2 * PI_F;
    while (a < -PI_F) a += 2 * PI_F;
    return a;
}

static inline M4 m4_identity(void) { M4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static inline M4 m4_mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static inline M4 m4_ortho(float l, float r, float b, float t, float n, float f) {
    M4 m; memset(&m, 0, sizeof m);
    m.m[0] = 2 / (r - l); m.m[5] = 2 / (t - b); m.m[10] = -2 / (f - n);
    m.m[12] = -(r + l) / (r - l); m.m[13] = -(t + b) / (t - b); m.m[14] = -(f + n) / (f - n); m.m[15] = 1;
    return m;
}
static inline M4 m4_lookdir(V3 eye, V3 fwd, V3 up) {
    V3 f = v3norm(fwd), s = v3norm(v3cross(f, up)), u = v3cross(s, f);
    M4 m = m4_identity();
    m.m[0] = s.x; m.m[4] = s.y; m.m[8] = s.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[12] = -v3dot(s, eye); m.m[13] = -v3dot(u, eye); m.m[14] = v3dot(f, eye);
    return m;
}
// translation * rotY(yaw) * scale
static inline M4 m4_trs_y(V3 p, float yaw, float sc) {
    float c = cosf(yaw) * sc, s = sinf(yaw) * sc;
    M4 m = m4_identity();
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c; m.m[5] = sc;
    m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z;
    return m;
}
// full rotation: yaw (Y) then pitch (X local) then roll (Z local)
static inline M4 m4_trs_ypr(V3 p, float yaw, float pitch, float roll, float sc) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    // R = Ry * Rx * Rz
    float r00 = cy * cr + sy * sp * sr, r01 = -cy * sr + sy * sp * cr, r02 = sy * cp;
    float r10 = cp * sr, r11 = cp * cr, r12 = -sp;
    float r20 = -sy * cr + cy * sp * sr, r21 = sy * sr + cy * sp * cr, r22 = cy * cp;
    M4 m = m4_identity();
    m.m[0] = r00 * sc; m.m[1] = r10 * sc; m.m[2] = r20 * sc;
    m.m[4] = r01 * sc; m.m[5] = r11 * sc; m.m[6] = r21 * sc;
    m.m[8] = r02 * sc; m.m[9] = r12 * sc; m.m[10] = r22 * sc;
    m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z;
    return m;
}

// deterministic hashing / RNG
static inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline uint32_t hash2i(int a, int b, uint32_t seed) {
    return hash32((uint32_t)a * 0x9E3779B1U ^ hash32((uint32_t)b * 0x85EBCA77U ^ seed));
}
static inline float hashf(uint32_t h) { return (h & 0xFFFFFF) / 16777216.0f; }
extern uint32_t g_rng;
static inline uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static inline float frnd(void) { return (rnd() & 0xFFFFFF) / 16777216.0f; }
static inline float frange(float a, float b) { return a + (b - a) * frnd(); }

void dbg_log(const char *fmt, ...);
