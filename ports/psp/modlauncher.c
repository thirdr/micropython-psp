// _launcher: private helpers for the frozen script launcher (launcher.py).
//
// Reads the buttons, and draws filled bars and 8x8 text straight into the
// debug screen's framebuffer at pixel positions. It's deliberately not a
// public API: the psp module (buttons, power, vsync) is designed separately,
// and the launcher can move over to it later.
#include <string.h>

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspge.h>
#include <pspiofilemgr.h>

#include "py/runtime.h"
#include "psp_emu.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define FB_STRIDE     512

// The debug screen draws into the start of VRAM, through the uncached
// mirror so writes reach the display without a cache flush.
static u32 *framebuffer(void) {
    return (u32 *)(0x40000000 | (u32)sceGeEdramGetAddr());
}

// 0xRRGGBB to the framebuffer's 8888 layout, 0xAABBGGRR.
static u32 to_pixel(mp_int_t rgb) {
    return 0xff000000 | ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
}

// fill(x, y, w, h, rgb): a solid rectangle, clipped to the screen.
static mp_obj_t launcher_fill(size_t n_args, const mp_obj_t *args) {
    mp_int_t x0 = mp_obj_get_int(args[0]);
    mp_int_t y0 = mp_obj_get_int(args[1]);
    mp_int_t x1 = x0 + mp_obj_get_int(args[2]);
    mp_int_t y1 = y0 + mp_obj_get_int(args[3]);
    u32 pixel = to_pixel(mp_obj_get_int(args[4]));
    x0 = MAX(x0, 0);
    y0 = MAX(y0, 0);
    x1 = MIN(x1, SCREEN_WIDTH);
    y1 = MIN(y1, SCREEN_HEIGHT);
    u32 *fb = framebuffer();
    for (mp_int_t y = y0; y < y1; y++) {
        u32 *row = fb + y * FB_STRIDE;
        for (mp_int_t x = x0; x < x1; x++) {
            row[x] = pixel;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(launcher_fill_obj, 5, 5, launcher_fill);

// text(x, y, s, rgb): 8x8 characters from pixel (x, y), with no background,
// so they draw over whatever fill() put down.
static mp_obj_t launcher_text(size_t n_args, const mp_obj_t *args) {
    mp_int_t x = mp_obj_get_int(args[0]);
    mp_int_t y = mp_obj_get_int(args[1]);
    size_t len;
    const char *s = mp_obj_str_get_data(args[2], &len);
    u32 pixel = to_pixel(mp_obj_get_int(args[3]));
    pspDebugScreenEnableBackColor(0);
    for (size_t i = 0; i < len && x + 8 <= SCREEN_WIDTH; i++, x += 8) {
        pspDebugScreenPutChar(x, y, pixel, (u8)s[i]);
    }
    pspDebugScreenEnableBackColor(1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(launcher_text_obj, 4, 4, launcher_text);

// console(): back to a clear, white-on-black debug console with the cursor
// at the top left, ready for a script's output.
static mp_obj_t launcher_console(void) {
    pspDebugScreenSetTextColor(0xffffffff);
    pspDebugScreenSetBackColor(0xff000000);
    pspDebugScreenEnableBackColor(1);
    pspDebugScreenClear();
    pspDebugScreenSetXY(0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_console_obj, launcher_console);

// buttons(): the buttons held now, as a bit mask. Waits for the next sample,
// once per frame, so a loop around it runs at about 60 Hz.
static mp_obj_t launcher_buttons(void) {
    static int initialised = 0;
    if (!initialised) {
        sceCtrlSetSamplingCycle(0);
        sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
        initialised = 1;
    }
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    return mp_obj_new_int_from_uint(pad.Buttons);
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_buttons_obj, launcher_buttons);

// reset_buttons(): make the next psp.pressed()/psp.released() calls count as
// first calls, and put the stick's dead zone back to the default, so a script
// doesn't inherit the previous script's input state.
void psp_buttons_reset(void);

static mp_obj_t launcher_reset_buttons(void) {
    psp_buttons_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_reset_buttons_obj, launcher_reset_buttons);

// headless(): True under PPSSPPHeadless, which has no buttons.
static mp_obj_t launcher_headless(void) {
    return mp_obj_new_bool(psp_emu_is_headless());
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_headless_obj, launcher_headless);

// log(s): a line to stdout only (PSPLINK, the emulator's log and headless
// output), not the screen, so it can't draw over the launcher.
static mp_obj_t launcher_log(mp_obj_t s_in) {
    size_t len;
    const char *s = mp_obj_str_get_data(s_in, &len);
    sceIoWrite(1, s, len);
    sceIoWrite(1, "\n", 1);
    if (psp_emu_is_headless()) {
        psp_emu_send_output(s, len);
        psp_emu_send_output("\n", 1);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(launcher_log_obj, launcher_log);

static const mp_rom_map_elem_t launcher_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__launcher) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&launcher_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&launcher_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_console), MP_ROM_PTR(&launcher_console_obj) },
    { MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&launcher_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_buttons), MP_ROM_PTR(&launcher_reset_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_headless), MP_ROM_PTR(&launcher_headless_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&launcher_log_obj) },

    { MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_INT(PSP_CTRL_UP) },
    { MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_INT(PSP_CTRL_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_INT(PSP_CTRL_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(PSP_CTRL_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_CROSS), MP_ROM_INT(PSP_CTRL_CROSS) },
    { MP_ROM_QSTR(MP_QSTR_CIRCLE), MP_ROM_INT(PSP_CTRL_CIRCLE) },
    { MP_ROM_QSTR(MP_QSTR_TRIANGLE), MP_ROM_INT(PSP_CTRL_TRIANGLE) },
    { MP_ROM_QSTR(MP_QSTR_SQUARE), MP_ROM_INT(PSP_CTRL_SQUARE) },
};
static MP_DEFINE_CONST_DICT(launcher_module_globals, launcher_module_globals_table);

const mp_obj_module_t launcher_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&launcher_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__launcher, launcher_module);
