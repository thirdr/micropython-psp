// _launcher: private helpers for the frozen script launcher (launcher.py).
//
// Reads the buttons, and draws filled bars and 8x8 text straight into the
// console's framebuffer at pixel positions. It's deliberately not a
// public API: the psp module (buttons, power, vsync) is designed separately,
// and the launcher can move over to it later.
#include <string.h>

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspiofilemgr.h>

#include "py/runtime.h"
#include "psp_display.h"
#include "psp_emu.h"
#include "psp_port.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272
#define FB_STRIDE     512

// The console draws into the start of VRAM, through the uncached
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
    for (size_t i = 0; i < len && x + 8 <= SCREEN_WIDTH; i++, x += 8) {
        psp_console_draw_char(x, y, pixel, (u8)s[i]);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(launcher_text_obj, 4, 4, launcher_text);

// console(): back to a clear, white-on-black console with the cursor at the
// top left and no colours set, ready for a script's output.
static mp_obj_t launcher_console(void) {
    psp_console_reset();
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

void psp_end_script(void) {
    psp_display_release();
    psp_audio_reset();
    psp_network_reset();
    psp_buttons_reset();
}

// end_script(): undo what a script left behind (see psp_port.h), so the next
// one starts afresh and the launcher has the screen again.
static mp_obj_t launcher_end_script(void) {
    psp_end_script();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_end_script_obj, launcher_end_script);

// release_display(): take the screen back from pspdisplay, if a script
// used it, keeping its last frame showing, so the console and launcher can
// draw again.
static mp_obj_t launcher_release_display(void) {
    psp_display_release();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_release_display_obj, launcher_release_display);

// screen_pixel(x, y): the pixel showing at (x, y) as 0xRRGGBB, read from
// whichever framebuffer the display is scanning out. For tests: it checks
// what's really on screen, e.g. after pspdisplay hands the screen back.
static mp_obj_t launcher_screen_pixel(mp_obj_t x_in, mp_obj_t y_in) {
    mp_int_t x = mp_obj_get_int(x_in);
    mp_int_t y = mp_obj_get_int(y_in);
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        mp_raise_ValueError(MP_ERROR_TEXT("off screen"));
    }
    void *top;
    int width, format;
    sceDisplayGetFrameBuf(&top, &width, &format, PSP_DISPLAY_SETBUF_IMMEDIATE);
    u32 p = ((u32 *)(0x40000000 | (u32)top))[y * width + x];
    return mp_obj_new_int(((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff));
}
static MP_DEFINE_CONST_FUN_OBJ_2(launcher_screen_pixel_obj, launcher_screen_pixel);

// console_cell(col, row): (character, foreground, background) of a console
// cell, the colours as 0xRRGGBB. For tests: exact text and colours, which
// the pixels alone can't tell apart easily.
static mp_obj_t launcher_console_cell(mp_obj_t col_in, mp_obj_t row_in) {
    mp_int_t col = mp_obj_get_int(col_in);
    mp_int_t row = mp_obj_get_int(row_in);
    if (col < 0 || col >= PSP_CONSOLE_COLUMNS || row < 0 || row >= PSP_CONSOLE_ROWS) {
        mp_raise_ValueError(MP_ERROR_TEXT("off screen"));
    }
    char ch;
    uint32_t fg, bg;
    psp_console_cell(col, row, &ch, &fg, &bg);
    mp_obj_t items[3] = {
        mp_obj_new_str(&ch, 1),
        mp_obj_new_int(fg),
        mp_obj_new_int(bg),
    };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_2(launcher_console_cell_obj, launcher_console_cell);

// console_cursor(): the console's cursor as (col, row), for tests.
static mp_obj_t launcher_console_cursor(void) {
    int col, row;
    psp_console_cursor(&col, &row);
    mp_obj_t items[2] = { MP_OBJ_NEW_SMALL_INT(col), MP_OBJ_NEW_SMALL_INT(row) };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(launcher_console_cursor_obj, launcher_console_cursor);

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
    { MP_ROM_QSTR(MP_QSTR_end_script), MP_ROM_PTR(&launcher_end_script_obj) },
    { MP_ROM_QSTR(MP_QSTR_release_display), MP_ROM_PTR(&launcher_release_display_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen_pixel), MP_ROM_PTR(&launcher_screen_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_cell), MP_ROM_PTR(&launcher_console_cell_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_cursor), MP_ROM_PTR(&launcher_console_cursor_obj) },
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
