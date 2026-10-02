// pspdisplay: the PSP screen for picovector.
//
//   from pspdisplay import screen, update
//
// screen is a 480x272 picovector image; draw into it, then update() shows
// it. The image's pixels (0xAABBGGRR, premultiplied) are the PSP's 8888
// format, so update() copies them to VRAM as they are: into the framebuffer
// not showing, which then shows from the next vblank.
//
// While a script has the screen, the console stays off it (print()
// still reaches the logs). The launcher calls psp_display_release() when the
// script ends: the last frame stays showing, and the next script that asks
// for screen gets a new, black one.
#include <string.h>

#include <pspdisplay.h>
#include <pspdmac.h>
#include <pspge.h>
#include <psputils.h>

#include "py/builtin.h"
#include "py/objarray.h"
#include "py/runtime.h"

#include "psp_display.h"

#define WIDTH (480)
#define HEIGHT (272)
#define ROW_BYTES (WIDTH * 4)
#define SCREEN_BYTES (ROW_BYTES * HEIGHT)
// The display reads rows 512 pixels apart.
#define VRAM_STRIDE (512)
#define VRAM_ROW_BYTES (VRAM_STRIDE * 4)
#define FRAME_BYTES (VRAM_ROW_BYTES * HEIGHT)

// The screen image, and its pixels: the image only points at them, so they
// need their own root pointer to stay alive.
MP_REGISTER_ROOT_POINTER(mp_obj_t psp_display_screen);
MP_REGISTER_ROOT_POINTER(void *psp_display_pixels);

// The VRAM framebuffer showing. Buffer 0 is the one the console and
// the launcher draw into; 1 sits right after it.
static int front = 0;

static uint8_t *vram(int i) {
    return (uint8_t *)sceGeEdramGetAddr() + i * FRAME_BYTES;
}

static void show(int i, int sync) {
    sceDisplaySetFrameBuf(vram(i), VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, sync);
    front = i;
}

bool psp_display_active(void) {
    return MP_STATE_VM(psp_display_screen) != MP_OBJ_NULL;
}

void psp_display_release(void) {
    if (!psp_display_active()) {
        return;
    }
    MP_STATE_VM(psp_display_screen) = MP_OBJ_NULL;
    MP_STATE_VM(psp_display_pixels) = NULL;
    if (front != 0) {
        sceDmacMemcpy(vram(0), vram(1), FRAME_BYTES);
        show(0, PSP_DISPLAY_SETBUF_IMMEDIATE);
    }
}

static mp_obj_t get_screen(void) {
    if (!psp_display_active()) {
        uint8_t *pixels = m_malloc(SCREEN_BYTES);
        memset(pixels, 0, SCREEN_BYTES);
        mp_obj_t picovector = mp_import_name(MP_QSTR_picovector, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t args[3] = {
            MP_OBJ_NEW_SMALL_INT(WIDTH),
            MP_OBJ_NEW_SMALL_INT(HEIGHT),
            mp_obj_new_bytearray_by_ref(SCREEN_BYTES, pixels),
        };
        mp_obj_t screen = mp_call_function_n_kw(mp_load_attr(picovector, MP_QSTR_image), 3, 0, args);
        MP_STATE_VM(psp_display_pixels) = pixels;
        MP_STATE_VM(psp_display_screen) = screen;
    }
    return MP_STATE_VM(psp_display_screen);
}

// update(): show the screen, from the next vblank, and wait for it.
static mp_obj_t pspdisplay_update(void) {
    get_screen();
    const uint8_t *src = MP_STATE_VM(psp_display_pixels);
    int back = front ^ 1;
    uint8_t *dst = vram(back);
    if (((uintptr_t)src & 15) == 0) {
        // DMA, a row at a time, as the rows are further apart in VRAM.
        sceKernelDcacheWritebackRange(src, SCREEN_BYTES);
        for (int y = 0; y < HEIGHT; y++) {
            sceDmacMemcpy(dst + y * VRAM_ROW_BYTES, src + y * ROW_BYTES, ROW_BYTES);
        }
    } else {
        // DMA needs 16-byte alignment; write through the uncached mirror.
        uint8_t *out = (uint8_t *)((uintptr_t)dst | 0x40000000);
        for (int y = 0; y < HEIGHT; y++) {
            memcpy(out + y * VRAM_ROW_BYTES, src + y * ROW_BYTES, ROW_BYTES);
        }
    }
    show(back, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pspdisplay_update_obj, pspdisplay_update);

// screen is made on first use, so importing the module alone leaves the
// console on screen. MP_OBJ_NULL for anything else: no such attribute.
static mp_obj_t pspdisplay_getattr(mp_obj_t attr) {
    if (mp_obj_str_get_qstr(attr) == MP_QSTR_screen) {
        return get_screen();
    }
    return MP_OBJ_NULL;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pspdisplay_getattr_obj, pspdisplay_getattr);

static const mp_rom_map_elem_t pspdisplay_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pspdisplay) },
    { MP_ROM_QSTR(MP_QSTR___getattr__), MP_ROM_PTR(&pspdisplay_getattr_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&pspdisplay_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH), MP_ROM_INT(WIDTH) },
    { MP_ROM_QSTR(MP_QSTR_HEIGHT), MP_ROM_INT(HEIGHT) },
};
static MP_DEFINE_CONST_DICT(pspdisplay_globals, pspdisplay_globals_table);

const mp_obj_module_t pspdisplay_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pspdisplay_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pspdisplay, pspdisplay_module);
