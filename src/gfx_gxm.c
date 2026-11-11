// sceGxm renderer for PS Vita (no vitaGL, no runtime shader compiler)
#include "gfx.h"
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include <stdio.h>
#include "shaders.h"

#define DISPLAY_STRIDE 1024
#define BUFFER_COUNT 3
#define MAX_PENDING_SWAPS 2
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define STATIC_VTX_BYTES   (16 * 1024 * 1024)
#define TEX_BYTES          (30 * 1024 * 1024)
#define TEMP_VTX_PER_FRAME (96 * 1024)

typedef struct { void *address; } DisplayData;

static SceGxmContext *ctx;
static SceGxmRenderTarget *rt;
static SceUID fb_uid[BUFFER_COUNT];
static void *fb_addr[BUFFER_COUNT];
static SceGxmColorSurface fb_surf[BUFFER_COUNT];
static SceGxmSyncObject *fb_sync[BUFFER_COUNT];
static SceGxmDepthStencilSurface ds_surf;
static int back_idx = 0, front_idx = 0;
static SceGxmShaderPatcher *patcher;
static SceGxmShaderPatcherId vid, fid;
static SceGxmVertexProgram *vprog;
static SceGxmFragmentProgram *fprog_opaque, *fprog_blend;
static const SceGxmProgramParameter *p_wvp, *p_tint;
static uint16_t *lin_indices;
static uint8_t *static_base; static size_t static_used;
static uint8_t *tex_base; static size_t tex_used;
static Vtx *temp_base[BUFFER_COUNT]; static int temp_used;
static SceGxmNotification notif[BUFFER_COUNT];
static volatile unsigned int *notif_region;
static unsigned int frame_counter;
static int cur_mode = -1;

static void *gpu_alloc(SceKernelMemBlockType type, size_t size, size_t align, int attribs, SceUID *uid) {
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) size = ALIGN(size, 256 * 1024);
    else size = ALIGN(size, 4 * 1024);
    (void)align;
    *uid = sceKernelAllocMemBlock("gpu", type, size, NULL);
    if (*uid < 0) return NULL;
    void *mem = NULL;
    if (sceKernelGetMemBlockBase(*uid, &mem) < 0) return NULL;
    if (sceGxmMapMemory(mem, size, attribs) < 0) { sceKernelFreeMemBlock(*uid); return NULL; }
    return mem;
}
static void *vusse_alloc(size_t size, unsigned *off) {
    SceUID uid; size = ALIGN(size, 4096);
    uid = sceKernelAllocMemBlock("vusse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    void *mem; sceKernelGetMemBlockBase(uid, &mem);
    sceGxmMapVertexUsseMemory(mem, size, off);
    return mem;
}
static void *fusse_alloc(size_t size, unsigned *off) {
    SceUID uid; size = ALIGN(size, 4096);
    uid = sceKernelAllocMemBlock("fusse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    void *mem; sceKernelGetMemBlockBase(uid, &mem);
    sceGxmMapFragmentUsseMemory(mem, size, off);
    return mem;
}
static void *patcher_host_alloc(void *user, unsigned size) { (void)user; return malloc(size); }
static void patcher_host_free(void *user, void *mem) { (void)user; free(mem); }

static void display_callback(const void *cb) {
    const DisplayData *d = (const DisplayData *)cb;
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof fb);
    fb.size = sizeof fb; fb.base = d->address; fb.pitch = DISPLAY_STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; fb.width = SCREEN_W; fb.height = SCREEN_H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

int gfx_init(void) {
    SceGxmInitializeParams ip;
    memset(&ip, 0, sizeof ip);
    ip.flags = 0; ip.displayQueueMaxPendingCount = MAX_PENDING_SWAPS;
    ip.displayQueueCallback = display_callback; ip.displayQueueCallbackDataSize = sizeof(DisplayData);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&ip) < 0) return -1;

    SceUID u;
    void *vdm = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    void *vring = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    void *fring = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    unsigned fusse_off;
    void *fusse = fusse_alloc(SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &fusse_off);

    SceGxmContextParams cp;
    memset(&cp, 0, sizeof cp);
    cp.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE); cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = vdm; cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = vring; cp.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    cp.fragmentRingBufferMem = fring; cp.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferMem = fusse; cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = fusse_off;
    if (sceGxmCreateContext(&cp, &ctx) < 0) return -2;

    SceGxmRenderTargetParams rp;
    memset(&rp, 0, sizeof rp);
    rp.flags = 0; rp.width = SCREEN_W; rp.height = SCREEN_H; rp.scenesPerFrame = 1;
    rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE; rp.multisampleLocations = 0; rp.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&rp, &rt) < 0) return -3;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        fb_addr[i] = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * DISPLAY_STRIDE * SCREEN_H, SCE_GXM_COLOR_SURFACE_ALIGNMENT,
                               SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &fb_uid[i]);
        if (!fb_addr[i]) return -4;
        memset(fb_addr[i], 0, 4 * DISPLAY_STRIDE * SCREEN_H);
        sceGxmColorSurfaceInit(&fb_surf[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                               SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, SCREEN_W, SCREEN_H, DISPLAY_STRIDE, fb_addr[i]);
        sceGxmSyncObjectCreate(&fb_sync[i]);
    }
    unsigned dw = ALIGN(SCREEN_W, SCE_GXM_TILE_SIZEX), dh = ALIGN(SCREEN_H, SCE_GXM_TILE_SIZEY);
    void *depth = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * dw * dh, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT,
                            SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &u);
    sceGxmDepthStencilSurfaceInit(&ds_surf, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, dw, depth, NULL);
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&ds_surf, 1.0f);
    sceGxmDepthStencilSurfaceSetBackgroundStencil(&ds_surf, 0);
    sceGxmDepthStencilSurfaceSetForceLoadMode(&ds_surf, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_DISABLED);
    sceGxmDepthStencilSurfaceSetForceStoreMode(&ds_surf, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_DISABLED);

    // shader patcher
    const unsigned pbsize = 64 * 1024, usize = 64 * 1024;
    void *pbuf = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, pbsize, 4, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &u);
    unsigned voff, foff;
    void *vu = vusse_alloc(usize, &voff);
    void *fu = fusse_alloc(usize, &foff);
    SceGxmShaderPatcherParams pp;
    memset(&pp, 0, sizeof pp);
    pp.userData = NULL; pp.hostAllocCallback = patcher_host_alloc; pp.hostFreeCallback = patcher_host_free;
    pp.bufferAllocCallback = NULL; pp.bufferFreeCallback = NULL; pp.bufferMem = pbuf; pp.bufferMemSize = pbsize;
    pp.vertexUsseAllocCallback = NULL; pp.vertexUsseFreeCallback = NULL; pp.vertexUsseMem = vu; pp.vertexUsseMemSize = usize; pp.vertexUsseOffset = voff;
    pp.fragmentUsseAllocCallback = NULL; pp.fragmentUsseFreeCallback = NULL; pp.fragmentUsseMem = fu; pp.fragmentUsseMemSize = usize; pp.fragmentUsseOffset = foff;
    if (sceGxmShaderPatcherCreate(&pp, &patcher) < 0) return -5;

    const SceGxmProgram *vgxp = (const SceGxmProgram *)texture_v_gxp;
    const SceGxmProgram *fgxp = (const SceGxmProgram *)texture_tint_f_gxp;
    if (sceGxmProgramCheck(vgxp) < 0 || sceGxmProgramCheck(fgxp) < 0) return -6;
    sceGxmShaderPatcherRegisterProgram(patcher, vgxp, &vid);
    sceGxmShaderPatcherRegisterProgram(patcher, fgxp, &fid);
    const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(vgxp, "aPosition");
    const SceGxmProgramParameter *pt = sceGxmProgramFindParameterByName(vgxp, "aTexcoord");
    p_wvp = sceGxmProgramFindParameterByName(vgxp, "wvp");
    p_tint = sceGxmProgramFindParameterByName(fgxp, "uTintColor");
    SceGxmVertexAttribute at[2];
    at[0].streamIndex = 0; at[0].offset = 0; at[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[0].componentCount = 3;
    at[0].regIndex = sceGxmProgramParameterGetResourceIndex(pa);
    at[1].streamIndex = 0; at[1].offset = 12; at[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[1].componentCount = 2;
    at[1].regIndex = sceGxmProgramParameterGetResourceIndex(pt);
    SceGxmVertexStream st;
    st.stride = sizeof(Vtx); st.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    if (sceGxmShaderPatcherCreateVertexProgram(patcher, vid, at, 2, &st, 1, &vprog) < 0) return -7;
    if (sceGxmShaderPatcherCreateFragmentProgram(patcher, fid, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                                  NULL, vgxp, &fprog_opaque) < 0) return -8;
    SceGxmBlendInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.colorMask = SCE_GXM_COLOR_MASK_ALL; bi.colorFunc = SCE_GXM_BLEND_FUNC_ADD; bi.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    bi.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; bi.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bi.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE; bi.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    if (sceGxmShaderPatcherCreateFragmentProgram(patcher, fid, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                                  &bi, vgxp, &fprog_blend) < 0) return -9;

    // linear index buffer 0..65535
    lin_indices = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 65536 * 2, 4, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    for (int i = 0; i < 65536; i++) lin_indices[i] = (uint16_t)i;
    // static pools (CDRAM: fastest for the GPU)
    static_base = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, STATIC_VTX_BYTES, 16, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    tex_base = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, TEX_BYTES, 16, SCE_GXM_MEMORY_ATTRIB_READ, &u);
    if (!static_base || !tex_base) return -10;
    static_used = tex_used = 0;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        temp_base[i] = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, TEMP_VTX_PER_FRAME * sizeof(Vtx), 16, SCE_GXM_MEMORY_ATTRIB_READ, &u);
        if (!temp_base[i]) return -11;
    }
    notif_region = sceGxmGetNotificationRegion();
    for (int i = 0; i < BUFFER_COUNT; i++) {
        notif[i].address = (volatile unsigned int *)&notif_region[i];
        notif[i].value = 0;
        notif_region[i] = 0;
    }
    back_idx = 0; front_idx = BUFFER_COUNT - 1;
    return 0;
}

void gfx_shutdown(void) {
    sceGxmFinish(ctx);
    sceGxmDisplayQueueFinish();
}

Vtx *gfx_static_vertices(int count) {
    size_t bytes = ALIGN((size_t)count * sizeof(Vtx), 16);
    if (static_used + bytes > STATIC_VTX_BYTES) return NULL;
    Vtx *p = (Vtx *)(static_base + static_used);
    static_used += bytes;
    return p;
}

static void *tex_alloc(size_t bytes, size_t align) {
    size_t off = ALIGN(tex_used, align);
    if (off + bytes > TEX_BYTES) return NULL;
    tex_used = off + bytes;
    return tex_base + off;
}

int gfx_tex_create_p8(GfxTex *t, int w, int h, const uint8_t *pal, const uint8_t *idx, int repeat) {
    uint8_t *pp = tex_alloc(1024, 64);
    uint8_t *data = tex_alloc((size_t)w * h, 64);
    if (!pp || !data) return -1;
    memcpy(pp, pal, 1024);
    memcpy(data, idx, (size_t)w * h);
    SceGxmTexture *gt = malloc(sizeof(SceGxmTexture));
    sceGxmTextureInitLinear(gt, data, SCE_GXM_TEXTURE_FORMAT_P8_ABGR, w, h, 0);
    sceGxmTextureSetPalette(gt, pp);
    sceGxmTextureSetMinFilter(gt, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(gt, SCE_GXM_TEXTURE_FILTER_POINT);
    SceGxmTextureAddrMode am = repeat ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
    sceGxmTextureSetUAddrMode(gt, am);
    sceGxmTextureSetVAddrMode(gt, am);
    t->w = w; t->h = h; t->handle = gt;
    return 0;
}

int gfx_tex_create_rgba(GfxTex *t, int w, int h, const uint8_t *rgba, int linear_filter) {
    uint8_t *data = tex_alloc((size_t)w * h * 4, 64);
    if (!data) return -1;
    memcpy(data, rgba, (size_t)w * h * 4);
    SceGxmTexture *gt = malloc(sizeof(SceGxmTexture));
    sceGxmTextureInitLinear(gt, data, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, 0);
    SceGxmTextureFilter f = linear_filter ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
    sceGxmTextureSetMinFilter(gt, f);
    sceGxmTextureSetMagFilter(gt, f);
    sceGxmTextureSetUAddrMode(gt, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(gt, SCE_GXM_TEXTURE_ADDR_CLAMP);
    t->w = w; t->h = h; t->handle = gt;
    return 0;
}

static GfxTex white_tex;
static int white_ready;

void gfx_begin_frame(float r, float g, float b) {
    if (!white_ready) {
        uint8_t px[8 * 8 * 4];
        memset(px, 255, sizeof px);
        gfx_tex_create_rgba(&white_tex, 8, 8, px, 0);
        white_ready = 1;
    }
    // make sure the GPU is done with this buffer's temporary vertices
    sceGxmNotificationWait(&notif[back_idx]);
    temp_used = 0;
    sceGxmBeginScene(ctx, 0, rt, NULL, NULL, fb_sync[back_idx], &fb_surf[back_idx], &ds_surf);
    sceGxmSetViewport(ctx, SCREEN_W * 0.5f, SCREEN_W * 0.5f, SCREEN_H * 0.5f, -SCREEN_H * 0.5f, 0.5f, 0.5f);
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    sceGxmSetVertexProgram(ctx, vprog);
    cur_mode = -1;
    sceGxmSetFrontStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
    // clear colour with a full screen quad at the far plane
    Vtx *q = gfx_temp_vertices(6);
    if (q) {
        const float X[6] = {-1, 1, 1, -1, 1, -1}, Y[6] = {-1, -1, 1, -1, 1, 1};
        for (int i = 0; i < 6; i++) { q[i].x = X[i]; q[i].y = Y[i]; q[i].z = 1.0f; q[i].u = 0.5f; q[i].v = 0.5f; }
        M4 id = m4_identity();
        float tint[4] = {r, g, b, 1};
        gfx_draw(q, 6, &white_tex, &id, tint, GFX_UI);
    }
}

Vtx *gfx_temp_vertices(int count) {
    if (temp_used + count > TEMP_VTX_PER_FRAME) return NULL;
    Vtx *p = temp_base[back_idx] + temp_used;
    temp_used += count;
    return p;
}

static void set_mode(int mode) {
    if (mode == cur_mode) return;
    if (cur_mode == GFX_SHADOW)
        sceGxmSetFrontStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
                                  SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0xFF);
    cur_mode = mode;
    if (mode == GFX_OPAQUE) {
        sceGxmSetFragmentProgram(ctx, fprog_opaque);
        sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
    } else if (mode == GFX_BLEND) {
        sceGxmSetFragmentProgram(ctx, fprog_blend);
        sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    } else if (mode == GFX_SHADOW) {
        sceGxmSetFragmentProgram(ctx, fprog_blend);
        sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
        sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetFrontStencilRef(ctx, 0);
        sceGxmSetFrontStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
                                  SCE_GXM_STENCIL_OP_INCR, 0xFF, 0xFF);
        return;
    } else {
        sceGxmSetFragmentProgram(ctx, fprog_blend);
        sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    }
}

void gfx_draw(const Vtx *v, int count, const GfxTex *tex, const M4 *mvp, const float tint[4], int mode) {
    if (count <= 0) return;
    if (!tex) tex = &white_tex;
    set_mode(mode);
    sceGxmSetFragmentTexture(ctx, 0, (const SceGxmTexture *)tex->handle);
    while (count > 0) {
        int n = count > 65535 ? 65535 - (65535 % 3) : count;
        void *ub;
        sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
        sceGxmSetUniformDataF(ub, p_wvp, 0, 16, mvp->m);
        void *fb;
        sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fb);
        sceGxmSetUniformDataF(fb, p_tint, 0, 4, tint);
        sceGxmSetVertexStream(ctx, 0, v);
        sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, lin_indices, n);
        v += n; count -= n;
    }
}

void gfx_end_frame(void) {
    notif_region[back_idx] = 0;
    notif[back_idx].value = 1;
    sceGxmEndScene(ctx, NULL, &notif[back_idx]);
    sceGxmPadHeartbeat(&fb_surf[back_idx], fb_sync[back_idx]);
    DisplayData d; d.address = fb_addr[back_idx];
    sceGxmDisplayQueueAddEntry(fb_sync[front_idx], fb_sync[back_idx], &d);
    front_idx = back_idx;
    back_idx = (back_idx + 1) % BUFFER_COUNT;
    frame_counter++;
}
