// pico3d on the PSP's GE (its 3D chip).
//
// picovector's pico3d draws on the CPU. This moves its draws to the GE
// without changing the pico3d module or the submodule: the linker wraps
// pico3d's draw, scene and depth-clear entry points (-Wl,--wrap, see
// picovector_psp.cmake), and each wrapper either gives the work to the GE or
// passes it on to pico3d's own CPU code.
//
// The GE only draws into VRAM, and a pico3d surface is a picovector image in
// RAM. So a batch copies the image into VRAM, the GE draws every mesh in it
// with its own transform, lighting, fog, texturing and depth test, and the
// image is copied back. Without queuing (the default) a batch is one
// render() or draw(); with pspdisplay.gpu3d(queue=True) it lasts until
// gpu3d_sync() or update().
//
// The CPU still helps in two places:
// - The GE drops a triangle whole if any corner lands outside its 4096 px
//   drawing space, which a triangle reaching past the camera does. A mesh
//   whose bounding box crosses the near plane or that guard band is clipped
//   here first, in model space, so the GE only gets triangles it can draw.
// - Matcap texture coordinates come from pico3d's own formula, per vertex.
//
// What the GE can't do goes to the CPU instead: normal maps, a surface over
// 512 px wide or taller than the VRAM left after the display's two
// framebuffers, a texture over 512 px. The GE lights per vertex with its own
// rounding, so shading is close to pico3d's but not identical.
//
// The GE keeps one depth buffer in VRAM, shared by every surface, as pico3d
// shares its working buffer. The CPU fallback has its own, so a surface
// drawn partly by each doesn't depth-test one against the other.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pspge.h>
#include <pspgu.h>
#include <pspthreadman.h>
#include <psputils.h>

#include "pv_bindings.hpp"
#include "types.h"
#include "pico3d.hpp"

#include "pico3d_ge.h"

using namespace picovector;

// pico3d's own functions, as the linker names them. --wrap sends the
// bindings' calls to the __wrap_ versions below; __real_ is pico3d's.
#define SYM_DRAW_MESH   "_ZN10picovector16pico3d_draw_meshEPNS_15pico3d_target_tEPKNS_13pico3d_mesh_tEPKNS_6mat4_tES7_PKNS_17pico3d_material_tENS_16pico3d_shading_tEPKNS_14pico3d_light_tEPNS_15pico3d_vcache_tES7_"
#define SYM_SCENE_ADD   "_ZN10picovector16pico3d_scene_addEPNS_14pico3d_scene_tEPKNS_15pico3d_target_tEPKNS_13pico3d_mesh_tEPKNS_6mat4_tESA_PKNS_17pico3d_material_tENS_16pico3d_shading_tEPKNS_14pico3d_light_tESA_PPKcb"
#define SYM_SCENE_DRAW  "_ZN10picovector17pico3d_scene_drawEPNS_14pico3d_scene_tEPNS_15pico3d_target_tEit"
#define SYM_DEPTH_CLEAR "_ZN10picovector18pico3d_depth_clearEPNS_15pico3d_target_tEt"

extern "C" {
  int real_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh, const mat4_t *model,
                     const mat4_t *view_proj, const pico3d_material_t *material,
                     pico3d_shading_t shading, const pico3d_light_t *light,
                     pico3d_vcache_t *vcache, const mat4_t *view) __asm__("__real_" SYM_DRAW_MESH);
  bool real_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t, const pico3d_mesh_t *mesh,
                      const mat4_t *model, const mat4_t *view_proj,
                      const pico3d_material_t *material, pico3d_shading_t shading,
                      const pico3d_light_t *light, const mat4_t *view, const char **why,
                      bool flat_depth) __asm__("__real_" SYM_SCENE_ADD);
  int real_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                      uint16_t clear_to) __asm__("__real_" SYM_SCENE_DRAW);
  void real_depth_clear(pico3d_target_t *t, uint16_t value) __asm__("__real_" SYM_DEPTH_CLEAR);
  mp_obj_t __real_scene_make_new_impl(const mp_obj_type_t *type, size_t n_args,
                                      size_t n_kw, const mp_obj_t *args);
  // pspgu's id for the list it queued with sceGuStart(GU_DIRECT)
  extern int ge_list_executed[2];
}

namespace {

  // ── VRAM and the display list ─────────────────────────────────────────────
  constexpr int VRAM_STRIDE = 512;                     // pixels a VRAM row
  constexpr uint32_t DISPLAY_BYTES = 2 * VRAM_STRIDE * 4 * 272;   // pspdisplay's framebuffers
  constexpr int MAX_WIDTH = 512;
  constexpr int GE_MAX_COUNT = 65535;                  // vertices a draw call

  // GE commands pspgu has no call for (or one with ambiguous arguments).
  enum {
    CMD_REGION1 = 21, CMD_REGION2 = 22,
    CMD_XSCALE = 66, CMD_YSCALE = 67, CMD_ZSCALE = 68,
    CMD_XCENTER = 69, CMD_YCENTER = 70, CMD_ZCENTER = 71,
    CMD_AMBIENT_ALPHA = 88, CMD_CLEAR = 211, CMD_SCISSOR1 = 212, CMD_SCISSOR2 = 213,
    CMD_MINZ = 214, CMD_MAXZ = 215,
  };

  constexpr size_t LIST_BYTES = 256 * 1024;
  constexpr size_t LIST_SPARE = 8 * 1024;              // start a new list below this
  alignas(64) uint32_t g_list[LIST_BYTES / 4];

  // Vertex data, copied textures and clear sprites for the list in flight.
  constexpr size_t ARENA_FIRST = 1u << 20, ARENA_MAX = 8u << 20, ARENA_LEAST = 64u << 10;

  // The target pixels and textures a queued batch reads, rooted until it ends
  // (pspdisplay.c registers the array as a root pointer).
  constexpr int KEEP_MAX = 16;

  // ── one submission ────────────────────────────────────────────────────────
  struct rec_t {
    const pico3d_mesh_t     *mesh;
    const pico3d_material_t *material;
    const pico3d_light_t    *light;
    mat4_t                   model, view_proj, view;
    bool                     has_view, flat_depth;
    pico3d_shading_t         shading;
  };

  // A scene's submissions, hung off its bin1 (see scene_make_new_impl below).
  constexpr uint32_t SCENE_MAGIC = 0x33444547;          // "GED3"
  enum { MODE_GE, MODE_CPU };
  struct ge_scene_t {
    uint32_t               magic;
    const pico3d_scene_t  *owner;
    uint32_t               mode;
    uint32_t               cap;
    rec_t *rec() { return (rec_t *)(this + 1); }
  };

  struct cache_t {
    const pico3d_mesh_t *mesh; int vtype; uint8_t *verts; uint16_t *idx;
    vec3_t centre; float scale;                         // its 16-bit positions' frame
  };

  struct state_t {
    bool tried, ready;
    uint8_t *edram;
    uint32_t color_off, depth_off;
    int rows;                                          // VRAM rows the target can have
    uint8_t *arena; size_t arena_cap, arena_used;
    float *dist; size_t dist_cap;                      // clip distances, 6 a vertex
    bool enabled = true, queue = false;
    // the batch in the list, and the image it draws into
    bool open;
    uintptr_t base; int sx, w, h, stride;
    // a depth clear owed to the GE (made while no batch was open)
    bool clear_pending; uint16_t clear_value; int clear_w, clear_h;
    // a depth clear owed to the CPU's buffer, made before its next draw
    bool cpu_clear_pending; pico3d_target_t cpu_clear_t; uint16_t cpu_clear_value;
    int keep_count;
    cache_t cache[8]; int cache_count;
    uint32_t serial;                                   // counts lists: what's in flight
    // the camera the GE's projection and view matrices were last set for
    bool cam_valid, cam_has_view;
    mat4_t cam_view_proj, cam_view, cam_R;
    // the state last sent, so a mesh like the one before sends only what differs
    bool keys_valid;
    struct { int x0, y0, x1, y1, depth, flat, cull, fog, alpha, cutoff;
             float flat_z, fog_near, fog_far; uint32_t fog_col; } k_fixed;
    struct { int lit, spec, type; float v[3]; uint32_t col, amb; float atten; } k_light;
    struct { int lit, colors, spec, shininess; uint32_t base, specular; } k_mat;
    bool k_textured;
    uint8_t *pre; size_t pre_cap;                      // a streamed mesh's vertices, ready
    uint8_t *mask; uint32_t mask_cap;                  // a clipped mesh's vertex masks
    int unkicked; size_t kicked_at;                    // submissions since the GE was last let go
    pico3d_ge_stats_t stats;
  };
  state_t g;

  // ── small helpers ─────────────────────────────────────────────────────────
  inline uint32_t *list_ptr() { return (uint32_t *)((uintptr_t)g_list | 0x40000000); }
  inline size_t align16(size_t n) { return (n + 15) & ~(size_t)15; }
  inline int pow2_at_least(int n) { int p = 4; while (p < n) p <<= 1; return p; }
  inline float *row(mat4_t &m, int r) { return &m.v00 + r * 4; }
  inline const float *row(const mat4_t &m, int r) { return &m.v00 + r * 4; }

  bool invert(const mat4_t &in, mat4_t &out) {
    float a[4][8];
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++) { a[r][c] = row(in, r)[c]; a[r][c + 4] = r == c ? 1.0f : 0.0f; }
    for (int c = 0; c < 4; c++) {
      int p = c;
      for (int r = c + 1; r < 4; r++) if (fabsf(a[r][c]) > fabsf(a[p][c])) p = r;
      if (fabsf(a[p][c]) < 1e-12f) return false;
      if (p != c) for (int k = 0; k < 8; k++) { float s = a[c][k]; a[c][k] = a[p][k]; a[p][k] = s; }
      float inv = 1.0f / a[c][c];
      for (int k = 0; k < 8; k++) a[c][k] *= inv;
      for (int r = 0; r < 4; r++) {
        if (r == c) continue;
        float f = a[r][c];
        if (f != 0.0f) for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
      }
    }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) row(out, r)[c] = a[r][c + 4];
    return true;
  }

  // pspgu takes column vectors: x is the first column.
  void set_matrix(int which, const mat4_t &m) {
    ScePspFMatrix4 o;
    o.x.x = m.v00; o.x.y = m.v10; o.x.z = m.v20; o.x.w = m.v30;
    o.y.x = m.v01; o.y.y = m.v11; o.y.z = m.v21; o.y.w = m.v31;
    o.z.x = m.v02; o.z.y = m.v12; o.z.z = m.v22; o.z.w = m.v32;
    o.w.x = m.v03; o.w.y = m.v13; o.w.z = m.v23; o.w.w = m.v33;
    sceGuSetMatrix(which, &o);
  }

  inline vec3_t position(const pico3d_mesh_t *m, uint32_t v) {
    if (m->positions_q) {
      const int16_t *q = m->positions_q + (size_t)v * 3;
      return vec3_t(m->q_bias.x + m->q_scale.x * (float)q[0],
                    m->q_bias.y + m->q_scale.y * (float)q[1],
                    m->q_bias.z + m->q_scale.z * (float)q[2]);
    }
    const float *p = m->positions + (size_t)v * 3;
    return vec3_t(p[0], p[1], p[2]);
  }

  // ── setup ─────────────────────────────────────────────────────────────────
  bool ge_init() {
    if (g.tried) return g.ready;
    g.tried = true;
    uint32_t size = sceGeEdramGetSize();
    g.edram = (uint8_t *)sceGeEdramGetAddr();
    if (size <= DISPLAY_BYTES) return false;
    int rows = (int)((size - DISPLAY_BYTES) / (VRAM_STRIDE * 6));   // 4 colour + 2 depth bytes
    if (rows < 16) return false;
    if (rows > 1023) rows = 1023;
    g.rows = rows;
    g.color_off = DISPLAY_BYTES;
    g.depth_off = DISPLAY_BYTES + (uint32_t)VRAM_STRIDE * 4 * (uint32_t)rows;
    // (the arena comes with the first 3D draw: pspdisplay's copies don't need it)
    // The list is written through the uncached mirror: drop any cached lines
    // first, so none is ever written back over it.
    sceKernelDcacheWritebackInvalidateRange(g_list, sizeof(g_list));
    sceGuInit();
    g.ready = true;
    return true;
  }

  // Can the GE draw into this target? (Block transfers need a row stride that
  // is a multiple of 8 pixels, and at most 1024.)
  bool target_ok(const pico3d_target_t *t) {
    if (!g.enabled || !t->color || !ge_init()) return false;
    if (t->width < 1 || t->height < 1 || t->width > MAX_WIDTH || t->height > g.rows) return false;
    if (t->color_stride % 8 || t->color_stride > 1024) return false;
    return true;
  }

  // ── the arena ─────────────────────────────────────────────────────────────
  size_t arena_free() {
    size_t at = align16(g.arena_used);
    return at < g.arena_cap ? g.arena_cap - at : 0;
  }

  uint8_t *arena_take(size_t n) {
    size_t at = align16(g.arena_used);
    if (at + n > g.arena_cap) return nullptr;
    g.arena_used = at + n;
    return g.arena + at;
  }

  // ── the list ──────────────────────────────────────────────────────────────
  void submit_and_wait() {
    // vertices, textures and the target were written through the cache
    sceKernelDcacheWritebackAll();
    sceGuFinish();
    uint32_t t0 = sceKernelGetSystemTimeLow();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    uint32_t us = sceKernelGetSystemTimeLow() - t0;
    g.stats.busy_us += us;
    g.stats.last_us += us;
  }

  bool list_low() { return (size_t)sceGuCheckList() > LIST_BYTES - LIST_SPARE; }

  // Let the GE start on what's in the list so far, while Python carries on.
  void kick() {
    g.unkicked = 0;
    g.kicked_at = (size_t)sceGuCheckList();
    sceKernelDcacheWritebackAll();
    sceGeListUpdateStallAddr(ge_list_executed[0], (uint8_t *)list_ptr() + sceGuCheckList());
  }

  void scissor(int x0, int y0, int x1, int y1) {
    sceGuSendCommandi(CMD_SCISSOR1, (y0 << 10) | x0);
    sceGuSendCommandi(CMD_SCISSOR2, ((y1 - 1) << 10) | (x1 - 1));
  }

  // NDC z -1..1 to depth 0..65535 (0 near, as pico3d), or every fragment at z.
  void depth_range(bool flat, float z) {
    sceGuSendCommandf(CMD_ZSCALE, flat ? 0.0f : 32767.5f);
    sceGuSendCommandf(CMD_ZCENTER, flat ? z : 32767.5f);
    sceGuSendCommandi(CMD_MINZ, 0);
    sceGuSendCommandi(CMD_MAXZ, 65535);
  }

  // The state every list starts with.
  void frame_state() {
    sceGuDrawBuffer(GU_PSM_8888, (void *)g.color_off, VRAM_STRIDE);
    sceGuDepthBuffer((void *)g.depth_off, VRAM_STRIDE);
    // the drawing region: nothing is drawn outside it, whatever the scissor
    sceGuSendCommandi(CMD_REGION1, 0);
    sceGuSendCommandi(CMD_REGION2, ((g.h - 1) << 10) | (g.w - 1));
    // pico3d's viewport: x = (ndc * 0.5 + 0.5) * w, y = (1 - (ndc * 0.5 + 0.5)) * h,
    // inside the GE's 4096 px space with the image's corner at 2048, 2048.
    sceGuOffset(2048, 2048);
    sceGuSendCommandf(CMD_XSCALE, g.w * 0.5f);
    sceGuSendCommandf(CMD_YSCALE, -g.h * 0.5f);
    sceGuSendCommandf(CMD_XCENTER, 2048.0f + g.w * 0.5f);
    sceGuSendCommandf(CMD_YCENTER, 2048.0f + g.h * 0.5f);
    depth_range(false, 0.0f);
    sceGuDepthFunc(GU_LESS);
    sceGuDepthMask(GU_FALSE);
    // Clip at the near plane and clamp depth, rather than dropping a triangle
    // with a corner past the near or far plane.
    sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_DITHER);
    // The colour buffer's alpha is the stencil: every pixel drawn gets 0xFF,
    // as pico3d writes.
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 0xFF, 0xFF);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuPixelMask(0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    for (int i = 1; i < 4; i++) sceGuDisable(GU_LIGHT0 + i);
    g.cam_valid = false;
    g.keys_valid = false;
    g.unkicked = 0;
    g.kicked_at = 0;
  }

  // Finish what's in the list, then carry on in a new one (the image stays in VRAM).
  void chunk_flush() {
    submit_and_wait();
    g.arena_used = 0;
    g.cache_count = 0;
    g.serial++;
    sceGuStart(GU_DIRECT, list_ptr());
    frame_state();
  }

  // Grow the arena to at least n bytes (only between lists).
  bool ensure_arena(size_t n) {
    if (g.arena_cap >= n) return true;
    if (n > ARENA_MAX) return false;
    if (g.open) chunk_flush();
    size_t want = g.arena_cap > ARENA_FIRST ? g.arena_cap : ARENA_FIRST;
    while (want < n) want *= 2;
    if (want > ARENA_MAX) want = ARENA_MAX;
    uint8_t *p = (uint8_t *)realloc(g.arena, want);
    while (!p && want / 2 >= n && want / 2 >= ARENA_LEAST) {
      want /= 2;
      p = (uint8_t *)realloc(g.arena, want);
    }
    if (!p) return false;
    g.arena = p;
    g.arena_cap = want;
    return true;
  }

  void emit_depth_clear(int w, int h, uint16_t value) {
    struct cv { uint32_t color; uint16_t x, y, z, pad; };
    cv *v = (cv *)arena_take(2 * sizeof(cv));
    if (!v) { chunk_flush(); v = (cv *)arena_take(2 * sizeof(cv)); }
    v[0] = { 0, 0, 0, value, 0 };
    v[1] = { 0, (uint16_t)w, (uint16_t)h, value, 0 };
    scissor(0, 0, w, h);
    sceGuSendCommandi(CMD_CLEAR, (GU_DEPTH_BUFFER_BIT << 8) | 1);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, nullptr, v);
    sceGuSendCommandi(CMD_CLEAR, 0);
  }

  void unkeep() {
    for (int i = 0; i < KEEP_MAX; i++) MP_STATE_VM(pico3d_ge_keep)[i] = nullptr;
    g.keep_count = 0;
  }

  bool keep(const void *p) {
    void **k = MP_STATE_VM(pico3d_ge_keep);
    for (int i = 0; i < g.keep_count; i++) if (k[i] == p) return true;
    if (g.keep_count == KEEP_MAX) return false;
    k[g.keep_count++] = (void *)p;
    return true;
  }

  // ── batches ───────────────────────────────────────────────────────────────
  void batch_end() {
    if (!g.open) return;
    sceGuCopyImage(GU_PSM_8888, 0, 0, g.w, g.h, VRAM_STRIDE, g.edram + g.color_off,
                   g.sx, 0, g.stride, (void *)g.base);
    sceGuTexSync();
    submit_and_wait();
    // The GE wrote the image behind the cache. Its lines are clean (written
    // back before the list ran), so this only drops them.
    sceKernelDcacheWritebackInvalidateRange((void *)g.base, (unsigned)(g.h * g.stride * 4 + g.sx * 4));
    g.open = false;
    g.arena_used = 0;
    g.cache_count = 0;
    unkeep();
  }

  void batch_begin(const pico3d_target_t *t) {
    uintptr_t at = (uintptr_t)t->color;
    uintptr_t base = at & ~(uintptr_t)15;
    int sx = (int)((at & 15) / 4);
    if (g.open && (g.base != base || g.sx != sx || g.w != t->width || g.h != t->height ||
                   g.stride != t->color_stride)) {
      batch_end();
    }
    if (g.open) return;
    g.open = true;
    g.base = base; g.sx = sx; g.w = t->width; g.h = t->height; g.stride = t->color_stride;
    g.arena_used = 0;
    g.cache_count = 0;
    g.stats.batches++;
    g.stats.last_us = 0;
    keep(t->color);
    g.serial++;
    sceGuStart(GU_DIRECT, list_ptr());
    frame_state();
    sceGuCopyImage(GU_PSM_8888, sx, 0, g.w, g.h, g.stride, (void *)base,
                   0, 0, VRAM_STRIDE, g.edram + g.color_off);
    sceGuTexSync();
    if (g.clear_pending) {
      g.clear_pending = false;
      emit_depth_clear(g.clear_w, g.clear_h, g.clear_value);
    }
  }

  // ── what a submission needs ───────────────────────────────────────────────
  struct prep_t {
    const pico3d_target_t *t;
    const rec_t *r;
    mat4_t mvp, mc;
    bool lit, spec, face, matcap, colors, alpha_test;
    const pico3d_texture_t *tex;
    int vtype, stride, o_uv, o_col, o_nrm, o_pos;
    bool flat; float flat_z;
    // Compact vertices (the indexed path): 16-bit positions in the mesh's own
    // box, centre +- scale, and 8-bit normals. The GE fetches each vertex once
    // for every triangle that uses it, so their size sets its speed.
    bool compact;
    vec3_t centre; float scale;
    const cache_t *cached;
  };

  // A vertex's layout. The GE puts each part at a multiple of its own size,
  // and the whole vertex at a multiple of its largest part's.
  void layout(prep_t &p, bool compact) {
    int o = 0, big = 1;
    auto at = [&](int align, int size) {
      o = (o + align - 1) & ~(align - 1);
      int r = o;
      o += size;
      if (align > big) big = align;
      return r;
    };
    p.compact = compact;
    p.vtype = GU_TRANSFORM_3D;
    if (p.tex) { p.o_uv = at(4, 8); p.vtype |= GU_TEXTURE_32BITF; }
    if (p.colors) { p.o_col = at(4, 4); p.vtype |= GU_COLOR_8888; }
    if (p.lit) {
      if (compact) { p.o_nrm = at(1, 3); p.vtype |= GU_NORMAL_8BIT; }
      else { p.o_nrm = at(4, 12); p.vtype |= GU_NORMAL_32BITF; }
    }
    if (compact) { p.o_pos = at(2, 6); p.vtype |= GU_VERTEX_16BIT; }
    else { p.o_pos = at(4, 12); p.vtype |= GU_VERTEX_32BITF; }
    p.stride = (o + big - 1) & ~(big - 1);
  }

  // Mirrors pico3d's prepare_draw(); false when the GE can't draw it.
  bool classify(const rec_t &r, prep_t &p) {
    const pico3d_mesh_t *mesh = r.mesh;
    const pico3d_material_t *m = r.material;
    p.r = &r;
    bool has_nmap = m->normal_map && mesh->tangents;
    bool has_spec = m->specular != 0;
    bool per_pixel = (has_nmap || has_spec) && r.light && mesh->normals;
    if (per_pixel && has_nmap) return false;            // normal maps stay on the CPU
    p.matcap = !per_pixel && m->matcap && mesh->normals;
    pico3d_shading_t shading = r.shading;
    if (shading == PICO3D_GOURAUD && !mesh->normals) shading = PICO3D_FLAT;
    if (!r.light) shading = PICO3D_UNLIT;
    p.lit = p.spec = p.face = false;
    if (per_pixel) {                                    // pico3d lights these per pixel
      p.lit = true;
      p.spec = has_spec && r.has_view;
    } else if (p.matcap) {
      // unlit: the base colour times the matcap
    } else if (shading == PICO3D_GOURAUD) {
      p.lit = true;
    } else if (shading == PICO3D_FLAT) {
      p.lit = p.face = true;
    }
    p.tex = p.matcap ? m->matcap : m->texture;
    if (p.tex && (p.tex->width < 1 || p.tex->height < 1 ||
                  p.tex->width > 512 || p.tex->height > 512)) return false;
    p.alpha_test = !p.matcap && m->texture && m->alpha_cutoff > 0;
    p.colors = mesh->colors != nullptr;
    layout(p, false);
    return true;
  }

  // Bytes the texture takes in the arena: 0 when the GE reads it in place.
  size_t texture_bytes(const prep_t &p) {
    if (!p.tex) return 0;
    int w = p.tex->width, h = p.tex->height;
    if (w == pow2_at_least(w) && h == pow2_at_least(h) &&
        ((uintptr_t)p.tex->texels & 15) == 0) return 0;
    return (size_t)pow2_at_least(w) * (size_t)pow2_at_least(h) * 4 + 16;
  }

  void sub_texture(const prep_t &p) {
    if (!p.tex) { sceGuDisable(GU_TEXTURE_2D); return; }
    const pico3d_texture_t *tx = p.tex;
    int w = tx->width, h = tx->height, w2 = pow2_at_least(w), h2 = pow2_at_least(h);
    const void *tbp = tx->texels;
    if (texture_bytes(p)) {
      // The GE wants power-of-two sizes: stretch the texture to one (nearest
      // texel), so it still tiles at its own edges as pico3d's does.
      uint32_t *dst = (uint32_t *)arena_take((size_t)w2 * h2 * 4);
      for (int y = 0; y < h2; y++) {
        const uint32_t *src = tx->texels + (size_t)(y * h / h2) * w;
        uint32_t *d = dst + (size_t)y * w2;
        for (int x = 0; x < w2; x++) d[x] = src[x * w / w2];
      }
      tbp = dst;
    }
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, w2, h2, w2, tbp);
    // pico3d wraps texture coordinates (a uv past 1 tiles)
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    bool bilinear = p.r->material->filter == PICO3D_BILINEAR;
    // pico3d's bilinear puts texel centres on whole coordinates, half a
    // texel from the GE's
    if (bilinear) sceGuTexOffset(0.5f / w2, 0.5f / h2); else sceGuTexOffset(0.0f, 0.0f);
    int f = bilinear ? GU_LINEAR : GU_NEAREST;
    sceGuTexFilter(f, f);
    sceGuTexFlush();
    sceGuEnable(GU_TEXTURE_2D);
  }

  // The projection and view for a camera (see sub_state).
  void camera(const rec_t &r) {
    mat4_t R, Rinv;
    if (r.has_view) {
      for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) row(R, i)[j] = row(r.view, i)[j];
      if (!invert(R, Rinv)) { R = mat4_t(); Rinv = mat4_t(); }
    }
    const float *r3 = row(r.view_proj, 3);
    float a[4];
    for (int k = 0; k < 4; k++)
      a[k] = r3[0] * row(Rinv, 0)[k] + r3[1] * row(Rinv, 1)[k] +
             r3[2] * row(Rinv, 2)[k] + r3[3] * row(Rinv, 3)[k];
    mat4_t V, Vinv;
    float ax = fabsf(a[0]), ay = fabsf(a[1]), az = fabsf(a[2]);
    if (ax + ay + az > 1e-9f) {
      int k = az >= ax && az >= ay ? 2 : (ay >= ax ? 1 : 0);
      int i = k == 0 ? 1 : 0, j = k == 2 ? 1 : 2;
      V.v00 = V.v11 = V.v22 = 0.0f;                     // from identity to all zero but v33
      row(V, 0)[i] = 1.0f;
      row(V, 1)[j] = 1.0f;
      for (int c = 0; c < 4; c++) row(V, 2)[c] = -a[c];
      V.v33 = 1.0f;
      if (!invert(V, Vinv)) { V = mat4_t(); Vinv = mat4_t(); }
    }
    set_matrix(GU_PROJECTION, r.view_proj * Rinv * Vinv);
    set_matrix(GU_VIEW, V);
    g.cam_valid = true;
    g.cam_has_view = r.has_view;
    g.cam_view_proj = r.view_proj;
    if (r.has_view) g.cam_view = r.view;
    g.cam_R = R;
  }

  // Everything a submission sets: matrices, depth, culling, light, material,
  // texture, fog, the clip.
  void sub_state(prep_t &p) {
    const rec_t &r = *p.r;
    const pico3d_target_t *t = p.t;

    // The GE lights in world space with the viewer fixed at +Z, and fogs on
    // the view matrix's z. So: rotate the world by the view's rotation (R),
    // which puts pico3d's camera direction (the view's row 2) on +Z; then a
    // view matrix whose z row is -w, so fog runs on the same eye distance
    // pico3d's does; and a projection that undoes both, leaving
    // P * V * W = view_proj * model.
    // Every mesh seen by one camera shares the projection and view, so they
    // are only worked out and sent when the camera changes.
    bool same = g.cam_valid && g.cam_has_view == r.has_view &&
                !memcmp(&g.cam_view_proj, &r.view_proj, sizeof(mat4_t)) &&
                (!r.has_view || !memcmp(&g.cam_view, &r.view, sizeof(mat4_t)));
    if (!same) camera(r);
    const mat4_t &R = g.cam_R;
    mat4_t W = R * r.model;
    if (p.compact) {
      // 16-bit positions read as q / 32768: scale and move them back into
      // the mesh's space. The scale is the same on every axis, so normals
      // keep their direction through it.
      mat4_t Q;
      Q.v00 = Q.v11 = Q.v22 = p.scale;
      Q.v03 = p.centre.x; Q.v13 = p.centre.y; Q.v23 = p.centre.z;
      W = W * Q;
    }
    set_matrix(GU_MODEL, W);
    if (p.matcap) p.mc = r.has_view ? r.view * r.model : r.model;

    // the image's clip rect, depth, culling, alpha test and fog
    const pico3d_material_t *m = r.material;
    auto &kf = g.k_fixed;
    decltype(g.k_fixed) f;
    memset(&f, 0, sizeof(f));
    f.x0 = t->clip_x0 < 0 ? 0 : t->clip_x0;
    f.y0 = t->clip_y0 < 0 ? 0 : t->clip_y0;
    f.x1 = t->clip_x1 > t->width ? t->width : t->clip_x1;
    f.y1 = t->clip_y1 > t->height ? t->height : t->clip_y1;
    f.depth = t->depth != nullptr;
    f.flat = p.flat; f.flat_z = p.flat ? p.flat_z : 0.0f;
    f.cull = !m->double_sided;
    f.alpha = p.alpha_test; f.cutoff = p.alpha_test ? m->alpha_cutoff : 0;
    f.fog = t->fog_far > t->fog_near;
    if (f.fog) { f.fog_near = t->fog_near; f.fog_far = t->fog_far; f.fog_col = t->fog & 0xFFFFFF; }
    if (!g.keys_valid || memcmp(&f, &kf, sizeof(f))) {
      scissor(f.x0, f.y0, f.x1, f.y1);
      if (f.depth) sceGuEnable(GU_DEPTH_TEST); else sceGuDisable(GU_DEPTH_TEST);
      depth_range(p.flat, p.flat_z);
      if (f.cull) {
        // pico3d's front faces wind anticlockwise on screen
        sceGuEnable(GU_CULL_FACE);
        sceGuFrontFace(GU_CCW);
      } else {
        sceGuDisable(GU_CULL_FACE);
      }
      if (f.alpha) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GEQUAL, f.cutoff, 0xFF);
      } else {
        sceGuDisable(GU_ALPHA_TEST);
      }
      if (f.fog) {
        sceGuEnable(GU_FOG);
        sceGuFog(f.fog_near, f.fog_far, f.fog_col);
      } else {
        sceGuDisable(GU_FOG);
      }
      kf = f;
    }

    // the light
    decltype(g.k_light) l;
    memset(&l, 0, sizeof(l));
    l.lit = p.lit;
    if (p.lit) {
      const pico3d_light_t *lt = r.light;
      if (lt->point && !p.face) {
        vec3_t pos = R.transform_direction(lt->position);
        l.v[0] = pos.x; l.v[1] = pos.y; l.v[2] = pos.z;
        l.type = GU_POINTLIGHT;
        l.atten = lt->atten;
      } else {
        // pico3d's FLAT faces take the light's direction even from a point light
        vec3_t d = R.transform_direction(-lt->direction).normalized();
        l.v[0] = d.x; l.v[1] = d.y; l.v[2] = d.z;
        l.type = GU_DIRECTIONAL;
      }
      l.spec = p.spec;
      l.col = lt->color & 0xFFFFFF;
      l.amb = lt->ambient & 0xFFFFFF;
    }
    bool light_changed = !g.keys_valid || memcmp(&l, &g.k_light, sizeof(l));
    if (light_changed) {
      if (l.lit) {
        ScePspFVector3 v = { l.v[0], l.v[1], l.v[2] };
        sceGuEnable(GU_LIGHTING);
        sceGuEnable(GU_LIGHT0);
        sceGuLight(0, l.type, l.spec ? GU_DIFFUSE_AND_SPECULAR : GU_AMBIENT_AND_DIFFUSE, &v);
        // pico3d's ambient isn't dimmed by distance, and the GE's light
        // ambient is, so it goes in the global ambient instead.
        sceGuLightColor(0, GU_AMBIENT, 0);
        sceGuLightColor(0, GU_DIFFUSE, l.col);
        sceGuLightColor(0, GU_SPECULAR, 0xFFFFFF);
        sceGuLightAtt(0, 1.0f, 0.0f, l.atten);
        sceGuAmbient(0xFF000000 | l.amb);
        sceGuLightMode(l.spec ? GU_SEPARATE_SPECULAR_COLOR : 0);
      } else {
        sceGuDisable(GU_LIGHTING);
      }
      g.k_light = l;
    }

    // the material
    decltype(g.k_mat) k;
    memset(&k, 0, sizeof(k));
    k.lit = p.lit; k.colors = p.colors; k.spec = p.spec;
    k.base = m->color & 0xFFFFFF;
    if (p.spec) { k.specular = m->specular & 0xFFFFFF; k.shininess = m->shininess; }
    if (light_changed || memcmp(&k, &g.k_mat, sizeof(k))) {
      if (k.lit) {
        sceGuSpecular((float)k.shininess);
        sceGuColorMaterial(k.colors ? (GU_AMBIENT | GU_DIFFUSE) : 0);
        sceGuModelColor(0, k.base, k.base, k.specular);
        sceGuSendCommandi(CMD_AMBIENT_ALPHA, 0xFF);
      } else {
        sceGuColor(0xFF000000 | k.base);
      }
      g.k_mat = k;
    }

    if (p.tex || !g.keys_valid || g.k_textured) sub_texture(p);
    g.k_textured = p.tex != nullptr;
    g.keys_valid = true;
  }

  // ── vertices ──────────────────────────────────────────────────────────────
  // A vertex on its way to the GE: model space, so the GE still lights it.
  struct cv_t { float p[3], n[3], uv[2], col[4], d[5]; };

  void matcap_uv(const prep_t &p, const vec3_t &n, float *uv) {
    vec3_t v = p.mc.transform_direction(n).normalized();
    uv[0] = v.x * 0.5f + 0.5f;
    uv[1] = 0.5f - v.y * 0.5f;
  }

  void fetch(const prep_t &p, uint32_t i, cv_t &o) {
    const pico3d_mesh_t *m = p.r->mesh;
    vec3_t pos = position(m, i);
    o.p[0] = pos.x; o.p[1] = pos.y; o.p[2] = pos.z;
    if (p.lit && !p.face) memcpy(o.n, m->normals + (size_t)i * 3, 12);
    if (p.tex) {
      if (p.matcap) {
        const float *n = m->normals + (size_t)i * 3;
        matcap_uv(p, vec3_t(n[0], n[1], n[2]), o.uv);
      } else if (m->uvs) {
        o.uv[0] = m->uvs[i * 2]; o.uv[1] = m->uvs[i * 2 + 1];
      } else {
        o.uv[0] = o.uv[1] = 0.0f;
      }
    }
    if (p.colors) {
      uint32_t c = m->colors[i];
      o.col[0] = (float)(c & 0xFF); o.col[1] = (float)((c >> 8) & 0xFF);
      o.col[2] = (float)((c >> 16) & 0xFF);
    }
  }

  inline void put(const prep_t &p, uint8_t *o, const cv_t &v) {
    if (p.tex) memcpy(o + p.o_uv, v.uv, 8);
    if (p.colors) {
      uint32_t c = 0xFF000000u | (uint32_t)(v.col[0] + 0.5f) | ((uint32_t)(v.col[1] + 0.5f) << 8) |
                   ((uint32_t)(v.col[2] + 0.5f) << 16);
      memcpy(o + p.o_col, &c, 4);
    }
    if (p.lit) memcpy(o + p.o_nrm, v.n, 12);
    memcpy(o + p.o_pos, v.p, 12);
  }

  // Does the whole mesh sit in front of the near plane and inside the GE's
  // drawing space? Then it can go as it is. (Its box, through the MVP.)
  void guards(const prep_t &p, float &gx, float &gy) {
    // stay 16 px inside the 4096 px space, the image's centre at 2048 + size/2
    gx = (2048.0f - p.t->width * 0.5f - 16.0f) / (p.t->width * 0.5f);
    gy = (2048.0f - p.t->height * 0.5f - 16.0f) / (p.t->height * 0.5f);
  }

  bool safe(const prep_t &p) {
    const pico3d_mesh_t *m = p.r->mesh;
    if (!m->has_bounds) return false;
    float gx, gy;
    guards(p, gx, gy);
    for (int c = 0; c < 8; c++) {
      vec3_t v(c & 1 ? m->bmax[0] : m->bmin[0], c & 2 ? m->bmax[1] : m->bmin[1],
               c & 4 ? m->bmax[2] : m->bmin[2]);
      vec4_t k = p.mvp * v;
      if (k.z + k.w <= 1e-6f) return false;
      if (fabsf(k.x) > gx * k.w || fabsf(k.y) > gy * k.w) return false;
    }
    return true;
  }

  // Before the indexed path's state: a copy already made this batch, or the
  // box its 16-bit positions will be measured in.
  void prepare_indexed(prep_t &p) {
    const pico3d_mesh_t *m = p.r->mesh;
    p.cached = nullptr;
    if (!p.matcap) {
      for (int i = 0; i < g.cache_count; i++) {
        if (g.cache[i].mesh == m && g.cache[i].vtype == p.vtype) {
          p.cached = &g.cache[i];
          p.centre = p.cached->centre;
          p.scale = p.cached->scale;
          return;
        }
      }
    }
    vec3_t lo = position(m, 0), hi = lo;
    for (uint32_t i = 1; i < m->vertex_count; i++) {
      vec3_t v = position(m, i);
      lo.x = fminf(lo.x, v.x); hi.x = fmaxf(hi.x, v.x);
      lo.y = fminf(lo.y, v.y); hi.y = fmaxf(hi.y, v.y);
      lo.z = fminf(lo.z, v.z); hi.z = fmaxf(hi.z, v.z);
    }
    float half = hi.x - lo.x;
    if (hi.y - lo.y > half) half = hi.y - lo.y;
    if (hi.z - lo.z > half) half = hi.z - lo.z;
    half = half * 0.5f;
    if (half < 1e-6f) half = 1e-6f;
    p.centre = vec3_t((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
    p.scale = half * (32768.0f / 32767.0f);
  }

  // (lrintf is a slow library call on the PSP)
  inline int round_int(float v) { return (int)(v < 0.0f ? v - 0.5f : v + 0.5f); }

  inline int16_t q16(float v) {
    int i = round_int(v);
    return (int16_t)(i < -32767 ? -32767 : (i > 32767 ? 32767 : i));
  }

  inline int8_t q8(float v) {
    int i = round_int(v * 127.0f);
    return (int8_t)(i < -127 ? -127 : (i > 127 ? 127 : i));
  }

  void put_compact(const prep_t &p, uint8_t *o, const cv_t &v) {
    if (p.tex) memcpy(o + p.o_uv, v.uv, 8);
    if (p.colors) {
      uint32_t c = 0xFF000000u | (uint32_t)(v.col[0] + 0.5f) | ((uint32_t)(v.col[1] + 0.5f) << 8) |
                   ((uint32_t)(v.col[2] + 0.5f) << 16);
      memcpy(o + p.o_col, &c, 4);
    }
    if (p.lit) {
      int8_t *n = (int8_t *)(o + p.o_nrm);
      n[0] = q8(v.n[0]); n[1] = q8(v.n[1]); n[2] = q8(v.n[2]);
    }
    float k = 32767.0f / (p.scale * (32767.0f / 32768.0f));
    int16_t q[3] = { q16((v.p[0] - p.centre.x) * k), q16((v.p[1] - p.centre.y) * k),
                     q16((v.p[2] - p.centre.z) * k) };
    memcpy(o + p.o_pos, q, 6);
  }

  // The indexed path: the mesh's vertices once, its indices as they are.
  // Meshes drawn more than once in a batch share one copy.
  bool emit_indexed(prep_t &p) {
    const pico3d_mesh_t *m = p.r->mesh;
    const uint32_t V = m->vertex_count, T = m->triangle_count;
    uint8_t *verts = p.cached ? p.cached->verts : nullptr;
    uint16_t *idx = p.cached ? p.cached->idx : nullptr;
    bool cacheable = !p.matcap;
    if (!verts) {
      size_t vbytes = (size_t)V * p.stride, ibytes = (size_t)T * 6;
      size_t need = align16(vbytes) + align16(ibytes) + 32;
      if (need > arena_free()) {
        if (need + texture_bytes(p) > g.arena_cap) return false;   // too big: stream it
        chunk_flush();
        sub_state(p);
        if (need > arena_free()) return false;
      }
      verts = arena_take(vbytes);
      idx = (uint16_t *)arena_take(ibytes);
      cv_t v;
      for (uint32_t i = 0; i < V; i++) {
        fetch(p, i, v);
        put_compact(p, verts + (size_t)i * p.stride, v);
      }
      memcpy(idx, m->indices, ibytes);
      if (cacheable && g.cache_count < (int)(sizeof(g.cache) / sizeof(g.cache[0]))) {
        g.cache[g.cache_count++] = { m, p.vtype, verts, idx, p.centre, p.scale };
      }
    }
    const uint32_t per = GE_MAX_COUNT / 3;
    for (uint32_t t0 = 0; t0 < T; t0 += per) {
      uint32_t n = T - t0 < per ? T - t0 : per;
      sceGuDrawArray(GU_TRIANGLES, p.vtype | GU_INDEX_16BIT, (int)(n * 3), idx + (size_t)t0 * 3, verts);
    }
    return true;
  }

  // ── meshes kept ready for the GE ──────────────────────────────────────────
  // The GE fetches indexed vertices from main RAM one by one, which costs it
  // about 5 times what reading them in order does (measured on a PSP-1000).
  // So a mesh is laid out once as three compact vertices a triangle, in
  // order, and kept for the next frame. A checksum of its arrays, taken the
  // first time it's drawn in a list, notices a script changing it.
  constexpr int MESHES_MAX = 32;
  constexpr size_t MESH_BYTES_MAX = 4u << 20;

  struct kept_t {
    const pico3d_mesh_t *mesh;
    uint32_t sum, vertices, triangles;
    int vtype; bool face;
    bool indexed;                                      // one vertex a mesh vertex (for clipping)
    uint8_t *data; size_t bytes;
    // what to draw: triangle strips (count vertices from first each), or
    // one list of triangles when there are none
    uint32_t *runs; int run_count;
    vec3_t centre; float scale;
    uint32_t checked;                                  // the list it was last checked in
    uint32_t used;                                     // the list it was last drawn in
  };
  kept_t g_kept[MESHES_MAX];
  size_t g_kept_bytes;

  // Fletcher-style: two adds a word, so it runs at memory speed (the
  // checksums come to well over 100 KB a frame for a scene like the demo's).
  // Words where the buffer allows, halfwords or bytes where it doesn't.
  struct sum_t { uint32_t a = 1, b = 0; };

  void mix(sum_t &s, const void *p, size_t bytes) {
    uint32_t a = s.a, b = s.b;
    if (((uintptr_t)p & 3) == 0) {
      const uint32_t *w = (const uint32_t *)p;
      size_t n = bytes / 4, i = 0;
      for (; i + 4 <= n; i += 4) {
        a += w[i]; b += a; a += w[i + 1]; b += a;
        a += w[i + 2]; b += a; a += w[i + 3]; b += a;
      }
      for (; i < n; i++) { a += w[i]; b += a; }
      p = w + n;
      bytes &= 3;
    } else if (((uintptr_t)p & 1) == 0) {
      const uint16_t *h = (const uint16_t *)p;
      for (size_t i = 0, n = bytes / 2; i < n; i++) { a += h[i]; b += a; }
      p = h + bytes / 2;
      bytes &= 1;
    }
    for (const uint8_t *c = (const uint8_t *)p; bytes--; c++) { a += *c; b += a; }
    s.a = a;
    s.b = b;
  }

  uint32_t checksum(const prep_t &p) {
    const pico3d_mesh_t *m = p.r->mesh;
    sum_t s;
    if (m->positions_q) mix(s, m->positions_q, (size_t)m->vertex_count * 6);
    else mix(s, m->positions, (size_t)m->vertex_count * 12);
    if (p.lit && !p.face) mix(s, m->normals, (size_t)m->vertex_count * 12);
    if (p.tex && m->uvs) mix(s, m->uvs, (size_t)m->vertex_count * 8);
    if (p.colors) mix(s, m->colors, (size_t)m->vertex_count * 4);
    mix(s, m->indices, (size_t)m->triangle_count * 6);
    return s.a ^ (s.b * 0x9E3779B1u);
  }

  void kept_free(kept_t &k) {
    free(k.data);
    free(k.runs);
    g_kept_bytes -= k.bytes;
    k = kept_t();
  }

  void kept_reset() {
    for (int i = 0; i < MESHES_MAX; i++) if (g_kept[i].data) kept_free(g_kept[i]);
  }

  // Room for another mesh: drop the least recently drawn that isn't in the
  // list in flight (finishing the list if they all are).
  kept_t *kept_slot(size_t bytes) {
    for (;;) {
      kept_t *empty = nullptr, *oldest = nullptr;
      for (int i = 0; i < MESHES_MAX; i++) {
        kept_t &k = g_kept[i];
        if (!k.data) { if (!empty) empty = &k; continue; }
        if (k.used == g.serial) continue;
        if (!oldest || k.used < oldest->used) oldest = &k;
      }
      if (empty && g_kept_bytes + bytes <= MESH_BYTES_MAX) return empty;
      if (oldest) { kept_free(*oldest); continue; }
      if (g_kept_bytes == 0) return empty;             // nothing to drop: too big alone
      chunk_flush();                                   // all in flight: finish them
    }
  }

  // ── triangle strips ───────────────────────────────────────────────────────
  // In a strip each triangle after the first takes one new vertex rather than
  // three, and the GE's time goes on vertices. A greedy walk: from each
  // triangle not yet used, step across shared edges while the next triangle
  // keeps the strip's alternating winding (so culling still sees the faces
  // as pico3d does). Strips join with zero-area triangles.
  struct edge_t { uint32_t key, tri; };

  int edge_cmp(const void *a, const void *b) {
    uint32_t x = ((const edge_t *)a)->key, y = ((const edge_t *)b)->key;
    return x < y ? -1 : (x > y ? 1 : 0);
  }

  // The strip vertex order, as indices, with -1 between strips. False when
  // memory runs out.
  bool stripify(const pico3d_mesh_t *m, int32_t **out, uint32_t *out_n) {
    const uint32_t T = m->triangle_count;
    const uint16_t *ix = m->indices;
    edge_t *edges = (edge_t *)malloc(sizeof(edge_t) * T * 3);
    int32_t *nb = (int32_t *)malloc(sizeof(int32_t) * T * 3);
    uint32_t *seen = (uint32_t *)calloc(T, sizeof(uint32_t));
    int32_t *walk = (int32_t *)malloc(sizeof(int32_t) * (T + 2));
    int32_t *res = (int32_t *)malloc(sizeof(int32_t) * (T * 3 + 16));
    if (!edges || !nb || !seen || !walk || !res) {
      free(edges); free(nb); free(seen); free(walk); free(res);
      return false;
    }
    for (uint32_t t = 0; t < T; t++) {
      for (int e = 0; e < 3; e++) {
        uint32_t a = ix[t * 3 + e], b = ix[t * 3 + (e + 1) % 3];
        edges[t * 3 + e] = { a < b ? (a << 16) | b : (b << 16) | a, t * 3 + (uint32_t)e };
        nb[t * 3 + e] = -1;
      }
    }
    qsort(edges, T * 3, sizeof(edge_t), edge_cmp);
    for (uint32_t i = 0; i + 1 < T * 3; ) {
      uint32_t j = i + 1;
      while (j < T * 3 && edges[j].key == edges[i].key) j++;
      if (j - i == 2) {                                // shared by exactly two
        nb[edges[i].tri] = (int32_t)(edges[i + 1].tri / 3);
        nb[edges[i + 1].tri] = (int32_t)(edges[i].tri / 3);
      }
      i = j;
    }
    free(edges);

    // One walk from triangle t0 turned by rot; marks with stamp, writes the
    // strip's vertices to walk[], returns their count.
    const uint32_t USED = 0xFFFFFFFFu;
    auto go = [&](uint32_t t0, int rot, uint32_t stamp) -> uint32_t {
      uint32_t n = 0;
      walk[n++] = ix[t0 * 3 + rot];
      walk[n++] = ix[t0 * 3 + (rot + 1) % 3];
      walk[n++] = ix[t0 * 3 + (rot + 2) % 3];
      seen[t0] = stamp;
      uint32_t cur = t0;
      for (;;) {
        int32_t s0 = walk[n - 2], s1 = walk[n - 1];
        int32_t next = -1;
        for (int e = 0; e < 3; e++) {
          int32_t a = ix[cur * 3 + e], b = ix[cur * 3 + (e + 1) % 3];
          if ((a == s0 && b == s1) || (a == s1 && b == s0)) { next = nb[cur * 3 + e]; break; }
        }
        if (next < 0 || seen[next] == USED || seen[next] == stamp) break;
        // the next triangle k = n - 2: (s0, s1, x) when k is even,
        // (s1, s0, x) when odd, as the GE reads it
        int32_t first = (n - 2) & 1 ? s1 : s0, second = (n - 2) & 1 ? s0 : s1;
        const uint16_t *v = ix + next * 3;
        int at = v[0] == first ? 0 : (v[1] == first ? 1 : (v[2] == first ? 2 : -1));
        if (at < 0 || v[(at + 1) % 3] != second) break;
        walk[n++] = v[(at + 2) % 3];
        seen[next] = stamp;
        cur = (uint32_t)next;
      }
      return n;
    };

    uint32_t rn = 0, stamp = 1;
    for (uint32_t t = 0; t < T; t++) {
      if (seen[t] == USED) continue;
      int best = 0; uint32_t best_n = 0;
      for (int rot = 0; rot < 3; rot++) {
        uint32_t n = go(t, rot, ++stamp);
        if (stamp == USED - 1) stamp = 1;
        if (n > best_n) { best_n = n; best = rot; }
      }
      uint32_t n = go(t, best, USED);
      if (rn) res[rn++] = -1;
      memcpy(res + rn, walk, n * sizeof(int32_t));
      rn += n;
    }
    free(nb); free(seen); free(walk);
    *out = res;
    *out_n = rn;
    return true;
  }

  // This submission's mesh laid out for the GE, made if it isn't kept yet.
  // Null when it can't be (no memory): the caller goes the indexed way.
  const kept_t *kept_mesh(prep_t &p, bool indexed) {
    const pico3d_mesh_t *m = p.r->mesh;
    kept_t *k = nullptr;
    for (int i = 0; i < MESHES_MAX; i++) {
      kept_t &e = g_kept[i];
      if (e.data && e.mesh == m && e.vtype == p.vtype && e.face == p.face && e.indexed == indexed) {
        k = &e;
        break;
      }
    }
    if (k && k->checked != g.serial) {
      // first time in this list: has the script changed the mesh?
      if (k->vertices != m->vertex_count || k->triangles != m->triangle_count ||
          k->sum != checksum(p)) {
        if (k->used == g.serial) chunk_flush();        // the GE may still be reading it
        kept_free(*k);
        k = nullptr;
      } else {
        k->checked = g.serial;
      }
    }
    if (!k) {
      int32_t *order = nullptr;
      uint32_t order_n = 0;
      uint32_t *runs = nullptr;
      int run_count = 0;
      uint32_t count = m->triangle_count * 3;
      uint32_t strips = 1;
      if (indexed) {
        count = m->vertex_count;
      } else if (!p.face && stripify(m, &order, &order_n)) {
        // the strips joined end to end, each draw at most GE_MAX_COUNT long
        uint32_t len = 0, longest = 0;
        for (uint32_t i = 0; i < order_n; i++) {
          if (order[i] < 0) { strips++; len = 0; } else if (++len > longest) longest = len;
        }
        if (longest > (uint32_t)GE_MAX_COUNT) { free(order); order = nullptr; }   // as triangles
      }
      if (order) {
        runs = (uint32_t *)malloc(sizeof(uint32_t) * 2 * strips);
        int32_t *joined = (int32_t *)malloc(sizeof(int32_t) * (order_n + 3 * strips));
        if (!runs || !joined) { free(runs); free(joined); free(order); runs = nullptr; order = nullptr; }
        else {
          uint32_t jn = 0, start = 0;
          for (uint32_t i = 0; i < order_n; ) {
            uint32_t e = i;
            while (e < order_n && order[e] >= 0) e++;
            uint32_t len = e - i;
            if (jn > start) {
              uint32_t in_run = jn - start;
              if (in_run + 3 + len > (uint32_t)GE_MAX_COUNT) {
                runs[run_count * 2] = start; runs[run_count * 2 + 1] = in_run; run_count++;
                start = jn;
              } else {
                // zero-area joins, keeping the next strip's first triangle even
                int32_t last = joined[jn - 1];
                joined[jn++] = last;
                joined[jn++] = order[i];
                if ((jn - start) & 1) joined[jn++] = order[i];
              }
            }
            memcpy(joined + jn, order + i, len * sizeof(int32_t));
            jn += len;
            i = e + 1;
          }
          if (jn > start) { runs[run_count * 2] = start; runs[run_count * 2 + 1] = jn - start; run_count++; }
          free(order);
          order = joined;
          count = jn;
        }
      }
      size_t bytes = (size_t)count * p.stride;
      kept_t *slot = kept_slot(bytes);
      uint8_t *data = slot ? (uint8_t *)malloc(bytes) : nullptr;
      if (!data) { free(order); free(runs); return nullptr; }
      prepare_indexed(p);                              // measures the box
      cv_t tri[3];
      uint8_t *o = data;
      if (indexed) {
        cv_t v;
        for (uint32_t i = 0; i < count; i++, o += p.stride) {
          fetch(p, i, v);
          put_compact(p, o, v);
        }
      } else if (order) {
        cv_t v;
        for (uint32_t i = 0; i < count; i++, o += p.stride) {
          fetch(p, (uint32_t)order[i], v);
          put_compact(p, o, v);
        }
        free(order);
      } else for (uint32_t f = 0; f < m->triangle_count; f++) {
        const uint16_t *ix = m->indices + (size_t)f * 3;
        for (int j = 0; j < 3; j++) fetch(p, ix[j], tri[j]);
        if (p.face) {
          vec3_t a(tri[0].p[0], tri[0].p[1], tri[0].p[2]);
          vec3_t n = (vec3_t(tri[1].p[0], tri[1].p[1], tri[1].p[2]) - a).cross(
                      vec3_t(tri[2].p[0], tri[2].p[1], tri[2].p[2]) - a).normalized();
          for (int j = 0; j < 3; j++) { tri[j].n[0] = n.x; tri[j].n[1] = n.y; tri[j].n[2] = n.z; }
        }
        for (int j = 0; j < 3; j++, o += p.stride) put_compact(p, o, tri[j]);
      }
      slot->mesh = m;
      slot->sum = checksum(p);
      slot->vertices = m->vertex_count;
      slot->triangles = m->triangle_count;
      slot->vtype = p.vtype;
      slot->face = p.face;
      slot->indexed = indexed;
      slot->data = data;
      slot->bytes = bytes;
      slot->runs = runs;
      slot->run_count = run_count;
      slot->centre = p.centre;
      slot->scale = p.scale;
      slot->checked = g.serial;
      g_kept_bytes += bytes;
      k = slot;
    }
    k->used = g.serial;
    p.centre = k->centre;
    p.scale = k->scale;
    return k;
  }

  void draw_kept(const prep_t &p, const kept_t *k) {
    if (k->runs) {
      for (int i = 0; i < k->run_count; i++) {
        sceGuDrawArray(GU_TRIANGLE_STRIP, p.vtype, (int)k->runs[i * 2 + 1], nullptr,
                       k->data + (size_t)k->runs[i * 2] * p.stride);
      }
      return;
    }
    const uint32_t per = GE_MAX_COUNT - GE_MAX_COUNT % 3;
    uint32_t total = k->triangles * 3;
    for (uint32_t v0 = 0; v0 < total; v0 += per) {
      uint32_t n = total - v0 < per ? total - v0 : per;
      sceGuDrawArray(GU_TRIANGLES, p.vtype, (int)n, nullptr, k->data + (size_t)v0 * p.stride);
    }
  }

  // The streaming path: three vertices a triangle, for flat faces (their own
  // normal), meshes that need clipping, and meshes too big for the arena.
  struct run_t { uint8_t *at; uint32_t n, cap; };

  void run_open(prep_t &p, run_t &r) {
    for (int tries = 0; ; tries++) {
      uint32_t cap = (uint32_t)(arena_free() / p.stride);
      if (cap > GE_MAX_COUNT) cap = GE_MAX_COUNT;
      cap -= cap % 3;
      if ((cap >= 3 * 64 || (tries > 0 && cap >= 3)) && !list_low()) {
        r.at = g.arena + align16(g.arena_used);
        r.n = 0;
        r.cap = cap;
        return;
      }
      chunk_flush();
      sub_state(p);
    }
  }

  void run_close(prep_t &p, run_t &r) {
    if (r.n) {
      sceGuDrawArray(GU_TRIANGLES, p.vtype, (int)r.n, nullptr, r.at);
      g.arena_used = (size_t)(r.at - g.arena) + (size_t)r.n * p.stride;
    }
    r.n = 0;
  }

  inline void push(prep_t &p, run_t &r, const cv_t &a, const cv_t &b, const cv_t &c) {
    if (r.n + 3 > r.cap) { run_close(p, r); run_open(p, r); }
    uint8_t *o = r.at + (size_t)r.n * p.stride;
    if (p.compact) {
      put_compact(p, o, a); put_compact(p, o + p.stride, b); put_compact(p, o + 2 * p.stride, c);
    } else {
      put(p, o, a); put(p, o + p.stride, b); put(p, o + 2 * p.stride, c);
    }
    r.n += 3;
  }

  inline cv_t lerp(const cv_t &a, const cv_t &b, float t) {
    cv_t o;
    const float *x = &a.p[0], *y = &b.p[0];
    float *z = &o.p[0];
    for (size_t k = 0; k < sizeof(cv_t) / sizeof(float); k++) z[k] = x[k] + (y[k] - x[k]) * t;
    return o;
  }

  // Cut a polygon by plane k (keep d[k] >= 0).
  int clip_by(const cv_t *in, int n, cv_t *out, int k) {
    int m = 0;
    for (int i = 0; i < n; i++) {
      const cv_t &a = in[i], &b = in[(i + 1) % n];
      bool ain = a.d[k] >= 0.0f, bin = b.d[k] >= 0.0f;
      if (ain) out[m++] = a;
      if (ain != bin) out[m++] = lerp(a, b, a.d[k] / (a.d[k] - b.d[k]));
    }
    return m;
  }

  bool emit_stream(prep_t &p, bool clip) {
    const pico3d_mesh_t *m = p.r->mesh;
    const uint32_t V = m->vertex_count, T = m->triangle_count;
    float gx = 0, gy = 0;
    if (clip) {
      // each vertex's distance inside the near plane and the four guard planes
      guards(p, gx, gy);
      size_t need = (size_t)V * 6;
      if (need > g.dist_cap) {
        float *d = (float *)realloc(g.dist, need * sizeof(float));
        if (!d) return false;
        g.dist = d;
        g.dist_cap = need;
      }
      for (uint32_t i = 0; i < V; i++) {
        vec4_t k = p.mvp * position(m, i);
        float *d = g.dist + (size_t)i * 6;
        d[0] = k.z + k.w;
        d[1] = gx * k.w - k.x; d[2] = gx * k.w + k.x;
        d[3] = gy * k.w - k.y; d[4] = gy * k.w + k.y;
        uint32_t bits = 0;
        for (int b = 0; b < 5; b++) if (d[b] < 0.0f) bits |= 1u << b;
        memcpy(&d[5], &bits, 4);
      }
    }
    // Each vertex formatted once: a triangle that needs no clipping is then
    // three copies. (Flat faces carry their own normal, so they can't share.)
    uint8_t *pre = nullptr;
    if (!p.face) {
      size_t need = (size_t)V * p.stride;
      if (need > g.pre_cap) {
        uint8_t *b = (uint8_t *)realloc(g.pre, need);
        if (b) { g.pre = b; g.pre_cap = need; }
      }
      if (need <= g.pre_cap) {
        pre = g.pre;
        cv_t v;
        for (uint32_t i = 0; i < V; i++) {
          fetch(p, i, v);
          put(p, pre + (size_t)i * p.stride, v);
        }
      }
    }
    run_t r;
    run_open(p, r);
    cv_t tri[3];
    for (uint32_t f = 0; f < T; f++) {
      const uint16_t *ix = m->indices + (size_t)f * 3;
      uint32_t bits[3] = { 0, 0, 0 };
      if (clip) {
        for (int k = 0; k < 3; k++) memcpy(&bits[k], &g.dist[(size_t)ix[k] * 6 + 5], 4);
        if (bits[0] & bits[1] & bits[2]) continue;      // all outside one plane
      }
      if (pre && !(bits[0] | bits[1] | bits[2])) {
        if (r.n + 3 > r.cap) { run_close(p, r); run_open(p, r); }
        uint8_t *o = r.at + (size_t)r.n * p.stride;
        for (int k = 0; k < 3; k++) memcpy(o + k * p.stride, pre + (size_t)ix[k] * p.stride, p.stride);
        r.n += 3;
        continue;
      }
      for (int k = 0; k < 3; k++) {
        fetch(p, ix[k], tri[k]);
        if (clip) memcpy(tri[k].d, &g.dist[(size_t)ix[k] * 6], 20);
      }
      if (p.face) {
        // pico3d lights a FLAT face by its own normal
        vec3_t a(tri[0].p[0], tri[0].p[1], tri[0].p[2]);
        vec3_t e1 = vec3_t(tri[1].p[0], tri[1].p[1], tri[1].p[2]) - a;
        vec3_t e2 = vec3_t(tri[2].p[0], tri[2].p[1], tri[2].p[2]) - a;
        vec3_t n = e1.cross(e2);
        for (int k = 0; k < 3; k++) { tri[k].n[0] = n.x; tri[k].n[1] = n.y; tri[k].n[2] = n.z; }
      }
      if (!clip || !(bits[0] | bits[1] | bits[2])) {
        push(p, r, tri[0], tri[1], tri[2]);
        continue;
      }
      cv_t a[9], b[9];
      int n = 3;
      a[0] = tri[0]; a[1] = tri[1]; a[2] = tri[2];
      cv_t *in = a, *out = b;
      for (int k = 0; k < 5 && n >= 3; k++) {
        if (!((bits[0] | bits[1] | bits[2]) & (1u << k))) continue;
        n = clip_by(in, n, out, k);
        cv_t *s = in; in = out; out = s;
      }
      for (int k = 2; k < n; k++) push(p, r, in[0], in[k - 1], in[k]);
    }
    run_close(p, r);
    return true;
  }

  // A mesh that needs clipping, from its kept vertices: a mask a vertex says
  // which planes it's outside; triangles inside all of them are copied
  // as they are, and only those across a plane are cut, on the CPU.
  void emit_clipped_kept(prep_t &p, const kept_t *k) {
    const pico3d_mesh_t *m = p.r->mesh;
    const uint32_t V = m->vertex_count, T = m->triangle_count;
    float gx, gy;
    guards(p, gx, gy);
    if (V > g.mask_cap) {
      uint8_t *b = (uint8_t *)realloc(g.mask, V);
      if (!b) return;
      g.mask = b;
      g.mask_cap = V;
    }
    uint8_t *mask = g.mask;
    const mat4_t &M = p.mvp;
    for (uint32_t i = 0; i < V; i++) {
      vec3_t v = position(m, i);
      float w = M.v30 * v.x + M.v31 * v.y + M.v32 * v.z + M.v33;
      float x = M.v00 * v.x + M.v01 * v.y + M.v02 * v.z + M.v03;
      float y = M.v10 * v.x + M.v11 * v.y + M.v12 * v.z + M.v13;
      float z = M.v20 * v.x + M.v21 * v.y + M.v22 * v.z + M.v23;
      mask[i] = (uint8_t)((z + w < 0.0f) | ((gx * w - x < 0.0f) << 1) | ((gx * w + x < 0.0f) << 2) |
                          ((gy * w - y < 0.0f) << 3) | ((gy * w + y < 0.0f) << 4));
    }
    auto distances = [&](cv_t &c) {
      vec4_t q = M * vec3_t(c.p[0], c.p[1], c.p[2]);
      c.d[0] = q.z + q.w;
      c.d[1] = gx * q.w - q.x; c.d[2] = gx * q.w + q.x;
      c.d[3] = gy * q.w - q.y; c.d[4] = gy * q.w + q.y;
    };
    const uint32_t stride = (uint32_t)p.stride;
    run_t r;
    run_open(p, r);
    for (uint32_t f = 0; f < T; f++) {
      const uint16_t *ix = m->indices + (size_t)f * 3;
      uint32_t b0 = mask[ix[0]], b1 = mask[ix[1]], b2 = mask[ix[2]];
      if (b0 & b1 & b2) continue;                      // all outside one plane
      if (!(b0 | b1 | b2)) {
        if (r.n + 3 > r.cap) { run_close(p, r); run_open(p, r); }
        uint8_t *o = r.at + (size_t)r.n * stride;
        memcpy(o, k->data + (size_t)ix[0] * stride, stride);
        memcpy(o + stride, k->data + (size_t)ix[1] * stride, stride);
        memcpy(o + 2 * stride, k->data + (size_t)ix[2] * stride, stride);
        r.n += 3;
        continue;
      }
      cv_t a[9], b[9];
      for (int j = 0; j < 3; j++) { fetch(p, ix[j], a[j]); distances(a[j]); }
      int n = 3;
      cv_t *in = a, *out = b;
      for (int j = 0; j < 5 && n >= 3; j++) {
        if (!((b0 | b1 | b2) & (1u << j))) continue;
        n = clip_by(in, n, out, j);
        cv_t *s = in; in = out; out = s;
      }
      for (int j = 2; j < n; j++) push(p, r, in[0], in[j - 1], in[j]);
    }
    run_close(p, r);
  }

  // The nearest vertex's depth, for flat_depth (pico3d puts the whole
  // submission on that one plane).
  bool nearest_depth(const prep_t &p, float &out) {
    const pico3d_mesh_t *m = p.r->mesh;
    float zmin = 3.4e38f;
    for (uint32_t i = 0; i < m->vertex_count; i++) {
      vec4_t k = p.mvp * position(m, i);
      if (k.z + k.w < 0.0f || k.w <= 0.0f) continue;
      float z = k.z / k.w;
      if (z < zmin) zmin = z;
    }
    if (zmin >= 3.4e38f) return false;
    float d = (zmin * 0.5f + 0.5f) * 65535.0f;
    out = d < 0.0f ? 0.0f : (d > 65535.0f ? 65535.0f : d);
    return true;
  }

  // One submission into the open batch. Returns its triangles (0 if culled).
  int emit(const pico3d_target_t *t, const rec_t &r) {
    prep_t p;
    p.t = t;
    classify(r, p);                                     // checked by the caller
    p.mvp = r.view_proj * r.model;
    if (pico3d_cull_mesh(r.mesh, &p.mvp)) return 0;
    if (t->clip_x1 <= t->clip_x0 || t->clip_y1 <= t->clip_y0) return 0;
    p.flat = r.flat_depth && nearest_depth(p, p.flat_z);
    if (p.tex && !texture_bytes(p) && !keep(p.tex->texels)) {
      // the batch reads too many textures in place: finish it, start another
      batch_end();
      batch_begin(t);
      keep(p.tex->texels);
    }
    if (list_low() || arena_free() < texture_bytes(p) + 1024) chunk_flush();
    bool clip = !safe(p);
    if (!clip && !p.matcap) {
      // the usual way: the mesh kept ready, in order (matcap's texture
      // coordinates follow the camera, so those meshes can't be kept)
      layout(p, true);
      if (const kept_t *k = kept_mesh(p, false)) {
        sub_state(p);
        draw_kept(p, k);
        g.stats.draws++;
        g.stats.triangles += r.mesh->triangle_count;
        return (int)r.mesh->triangle_count;
      }
      layout(p, false);
    }
    if (clip && !p.face && !p.matcap) {
      layout(p, true);
      if (const kept_t *k = kept_mesh(p, true)) {
        sub_state(p);
        emit_clipped_kept(p, k);
        g.stats.draws++;
        g.stats.triangles += r.mesh->triangle_count;
        return (int)r.mesh->triangle_count;
      }
      layout(p, false);
    }
    bool indexed = !p.face && !clip;
    if (indexed) {
      layout(p, true);
      prepare_indexed(p);
    }
    sub_state(p);
    if (!indexed || !emit_indexed(p)) {
      if (indexed) {                                    // too big for the arena
        layout(p, false);
        sub_state(p);
      }
      emit_stream(p, clip);
    }
    g.stats.draws++;
    g.stats.triangles += r.mesh->triangle_count;
    return (int)r.mesh->triangle_count;
  }

  // Draw these submissions on the GE; false (having drawn nothing) when one
  // of them can't be.
  bool ge_draw(const pico3d_target_t *t, const rec_t *recs, uint32_t n, int *drawn) {
    size_t most = 0;
    for (uint32_t i = 0; i < n; i++) {
      prep_t p;
      p.t = t;
      if (!classify(recs[i], p)) return false;
      size_t tb = texture_bytes(p);
      if (tb > most) most = tb;
    }
    if (!ensure_arena(most + ARENA_LEAST)) return false;
    *drawn = 0;
    if (n == 0) return true;
    batch_begin(t);
    int total = 0;
    for (uint32_t i = 0; i < n; i++) {
      total += emit(t, recs[i]);
      // let the GE at it every few meshes (a kick costs ~25 us of CPU)
      if (++g.unkicked >= 4 || (size_t)sceGuCheckList() - g.kicked_at > 16 * 1024) kick();
    }
    if (g.unkicked) kick();
    if (!g.queue) batch_end();
    *drawn = total;
    return true;
  }

  // Before pico3d's CPU code draws: the image has to hold the GE's work, and
  // the CPU's depth buffer any clear it was owed.
  void cpu_prepare() {
    batch_end();
    if (g.cpu_clear_pending) {
      g.cpu_clear_pending = false;
      real_depth_clear(&g.cpu_clear_t, g.cpu_clear_value);
    }
  }

  ge_scene_t *ge_scene_of(const pico3d_scene_t *sc) {
    ge_scene_t *gs = (ge_scene_t *)sc->bin1;
    return gs && gs->magic == SCENE_MAGIC && gs->owner == sc ? gs : nullptr;
  }

  // Hand a scene's submissions to the CPU: pico3d transforms them now.
  void scene_to_cpu(pico3d_scene_t *sc, ge_scene_t *gs, const pico3d_target_t *t) {
    if (gs->mode == MODE_GE) {
      uint32_t n = sc->sub_count;
      pico3d_scene_reset(sc);
      rec_t *rec = gs->rec();
      for (uint32_t i = 0; i < n; i++) {
        const rec_t &r = rec[i];
        real_scene_add(sc, t, r.mesh, &r.model, &r.view_proj, r.material, r.shading,
                       r.light, r.has_view ? &r.view : nullptr, nullptr, r.flat_depth);
      }
    }
    gs->mode = MODE_CPU;
  }

  void fill(rec_t &r, const pico3d_mesh_t *mesh, const mat4_t *model, const mat4_t *view_proj,
            const pico3d_material_t *material, pico3d_shading_t shading,
            const pico3d_light_t *light, const mat4_t *view, bool flat_depth) {
    r.mesh = mesh; r.material = material; r.light = light;
    r.model = *model; r.view_proj = *view_proj;
    r.has_view = view != nullptr;
    if (view) r.view = *view;
    r.shading = shading;
    r.flat_depth = flat_depth;
  }
}

// ── the wrappers ────────────────────────────────────────────────────────────
extern "C" {

  int wrap_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh, const mat4_t *model,
                     const mat4_t *view_proj, const pico3d_material_t *material,
                     pico3d_shading_t shading, const pico3d_light_t *light,
                     pico3d_vcache_t *vcache, const mat4_t *view) __asm__("__wrap_" SYM_DRAW_MESH);
  int wrap_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh, const mat4_t *model,
                     const mat4_t *view_proj, const pico3d_material_t *material,
                     pico3d_shading_t shading, const pico3d_light_t *light,
                     pico3d_vcache_t *vcache, const mat4_t *view) {
    if (target_ok(t)) {
      rec_t r;
      fill(r, mesh, model, view_proj, material, shading, light, view, false);
      int drawn;
      if (ge_draw(t, &r, 1, &drawn)) return drawn;
    }
    if (g.enabled) g.stats.fallbacks++;
    cpu_prepare();
    return real_draw_mesh(t, mesh, model, view_proj, material, shading, light, vcache, view);
  }

  bool wrap_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t, const pico3d_mesh_t *mesh,
                      const mat4_t *model, const mat4_t *view_proj,
                      const pico3d_material_t *material, pico3d_shading_t shading,
                      const pico3d_light_t *light, const mat4_t *view, const char **why,
                      bool flat_depth) __asm__("__wrap_" SYM_SCENE_ADD);
  bool wrap_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t, const pico3d_mesh_t *mesh,
                      const mat4_t *model, const mat4_t *view_proj,
                      const pico3d_material_t *material, pico3d_shading_t shading,
                      const pico3d_light_t *light, const mat4_t *view, const char **why,
                      bool flat_depth) {
    ge_scene_t *gs = ge_scene_of(sc);
    if (gs && sc->sub_count == 0) gs->mode = MODE_GE;   // each fill decides afresh
    prep_t p;
    rec_t r;
    fill(r, mesh, model, view_proj, material, shading, light, view, flat_depth);
    if (!gs || gs->mode == MODE_CPU || !target_ok(t) || !classify(r, p)) {
      if (gs) scene_to_cpu(sc, gs, t);
      return real_scene_add(sc, t, mesh, model, view_proj, material, shading, light, view,
                            why, flat_depth);
    }
    // Recorded, not transformed: the GE does that at draw(). The checks and
    // the counts are pico3d's own, so add() answers exactly as it would.
    if (why) *why = nullptr;
    mat4_t mvp = (*view_proj) * (*model);
    if (pico3d_cull_mesh(mesh, &mvp)) return true;
    if (sc->sub_count >= sc->sub_cap) { if (why) *why = "meshes"; return false; }
    if (sc->vert_count + mesh->vertex_count > sc->vert_cap) { if (why) *why = "vertices"; return false; }
    if (sc->tri_count + mesh->triangle_count > sc->tri_cap) { if (why) *why = "triangles"; return false; }
    if (mesh->triangle_count > sc->bin_cap) { if (why) *why = "bin"; return false; }
    gs->rec()[sc->sub_count++] = r;
    sc->vert_count += mesh->vertex_count;
    sc->tri_count += mesh->triangle_count;
    return true;
  }

  int wrap_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                      uint16_t clear_to) __asm__("__wrap_" SYM_SCENE_DRAW);
  int wrap_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                      uint16_t clear_to) {
    ge_scene_t *gs = ge_scene_of(sc);
    // banding only happens on surfaces too big for the GE
    if (gs && gs->mode == MODE_GE && band_rows <= 0 && target_ok(t)) {
      int drawn;
      if (ge_draw(t, gs->rec(), sc->sub_count, &drawn)) return drawn;
    }
    if (gs && sc->sub_count) scene_to_cpu(sc, gs, t);
    if (g.enabled) g.stats.fallbacks++;
    cpu_prepare();
    return real_scene_draw(sc, t, band_rows, clear_to);
  }

  void wrap_depth_clear(pico3d_target_t *t, uint16_t value) __asm__("__wrap_" SYM_DEPTH_CLEAR);
  void wrap_depth_clear(pico3d_target_t *t, uint16_t value) {
    if (!g.enabled || !ge_init()) {
      g.cpu_clear_pending = false;
      real_depth_clear(t, value);
      return;
    }
    // The CPU's buffer is only cleared if a CPU draw comes next.
    g.cpu_clear_pending = true;
    g.cpu_clear_t = *t;
    g.cpu_clear_value = value;
    int w = t->width < MAX_WIDTH ? t->width : MAX_WIDTH;
    int h = t->height < g.rows ? t->height : g.rows;
    if (w < 1 || h < 1) return;
    if (g.open) {
      if (list_low()) chunk_flush();
      emit_depth_clear(w, h, value);
    } else {
      g.clear_pending = true;
      g.clear_value = value;
      g.clear_w = w;
      g.clear_h = h;
    }
  }

  // A scene gets room for its submissions, hung off bin1: pico3d only uses
  // bin1 to share bands with a second core, which the PSP build hasn't got.
  // The scene object is scanned, so the GC keeps the records with it.
  mp_obj_t __wrap_scene_make_new_impl(const mp_obj_type_t *type, size_t n_args,
                                      size_t n_kw, const mp_obj_t *args) {
    mp_obj_t o = __real_scene_make_new_impl(type, n_args, n_kw, args);
    scene_obj_t *so = (scene_obj_t *)MP_OBJ_TO_PTR(o);
    uint32_t cap = so->scene.sub_cap;
    ge_scene_t *gs = (ge_scene_t *)m_malloc(sizeof(ge_scene_t) + (size_t)cap * sizeof(rec_t));
    gs->magic = SCENE_MAGIC;
    gs->owner = &so->scene;
    gs->mode = MODE_GE;
    gs->cap = cap;
    so->scene.bin1 = (uint16_t *)gs;
    return o;
  }

  // ── for pspdisplay and the launcher ───────────────────────────────────────
  void pico3d_ge_sync(void) {
    batch_end();
  }

  void pico3d_ge_set(int enabled, int queue) {
    batch_end();
    if (enabled >= 0) g.enabled = enabled != 0;
    if (queue >= 0) g.queue = queue != 0;
  }

  void pico3d_ge_reset(void) {
    batch_end();
    kept_reset();
    g.enabled = true;
    g.queue = false;
    g.clear_pending = false;
    g.cpu_clear_pending = false;
    memset(&g.stats, 0, sizeof(g.stats));
  }

  bool pico3d_ge_copy_to_vram(const void *src, int src_stride, void *dst, int dst_stride,
                              int w, int h) {
    if (!ge_init() || ((uintptr_t)src & 15) || ((uintptr_t)dst & 15) ||
        src_stride % 8 || dst_stride % 8 || src_stride > 1024 || dst_stride > 1024 ||
        w > 1024 || h > 1024) return false;
    batch_end();
    sceKernelDcacheWritebackAll();
    sceGuStart(GU_DIRECT, list_ptr());
    sceGuCopyImage(GU_PSM_8888, 0, 0, w, h, src_stride, (void *)src, 0, 0, dst_stride, dst);
    sceGuTexSync();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    return true;
  }

  void pico3d_ge_get_stats(pico3d_ge_stats_t *out) {
    *out = g.stats;
    out->enabled = g.enabled;
    out->queue = g.queue;
  }
}
