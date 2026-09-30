// psp: the PSP's own hardware from Python (buttons, analog stick, display
// refresh, battery and clock).
//
//   import psp
//   while True:
//       if psp.CROSS in psp.pressed():
//           ...
//       x, y = psp.analog()
//       psp.vsync()
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>

#include "py/mperrno.h"
#include "py/runtime.h"
#include "psp_emu.h"

// Analog sampling is set up on first use. The launcher's _launcher module
// sets digital-only sampling for itself, but analog mode reads the buttons
// just the same.
static void ctrl_init(void) {
    static int initialised = 0;
    if (!initialised) {
        sceCtrlSetSamplingCycle(0);
        sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
        initialised = 1;
    }
}

static void ctrl_read(SceCtrlData *pad) {
    ctrl_init();
    sceCtrlPeekBufferPositive(pad, 1);
}

// The buttons ordinary programs can read, in the order held(), pressed() and
// released() list them. HOME, the volume and screen buttons and the HOLD switch are only
// visible to kernel-mode code.
static const struct {
    u32 mask;
    qstr name;
} buttons[] = {
    { PSP_CTRL_UP, MP_QSTR_UP },
    { PSP_CTRL_DOWN, MP_QSTR_DOWN },
    { PSP_CTRL_LEFT, MP_QSTR_LEFT },
    { PSP_CTRL_RIGHT, MP_QSTR_RIGHT },
    { PSP_CTRL_CROSS, MP_QSTR_CROSS },
    { PSP_CTRL_CIRCLE, MP_QSTR_CIRCLE },
    { PSP_CTRL_TRIANGLE, MP_QSTR_TRIANGLE },
    { PSP_CTRL_SQUARE, MP_QSTR_SQUARE },
    { PSP_CTRL_LTRIGGER, MP_QSTR_L },
    { PSP_CTRL_RTRIGGER, MP_QSTR_R },
    { PSP_CTRL_START, MP_QSTR_START },
    { PSP_CTRL_SELECT, MP_QSTR_SELECT },
};

// The buttons in a mask, as a list of names such as ["CROSS", "UP"].
static mp_obj_t button_list(u32 mask) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < MP_ARRAY_SIZE(buttons); i++) {
        if (mask & buttons[i].mask) {
            mp_obj_list_append(list, MP_OBJ_NEW_QSTR(buttons[i].name));
        }
    }
    return list;
}

// held(): the buttons held down now, e.g. ["CROSS", "UP"], or [] for none.
// Doesn't wait; call vsync() in the loop to run once per frame.
static mp_obj_t psp_held(void) {
    SceCtrlData pad;
    ctrl_read(&pad);
    return button_list(pad.Buttons);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_held_obj, psp_held);

// pressed() and released() each keep their own copy of the last buttons they
// saw, so calling both in the same frame works, and neither hides the other's
// changes. The first call only notes what's held and reports nothing.
typedef struct {
    int primed;
    u32 previous;
} button_edges_t;

static button_edges_t pressed_edges;
static button_edges_t released_edges;

// The buttons whose state changed since the last call with this `edges`,
// keeping those that are now down (want_down) or now up.
static u32 button_changes(button_edges_t *edges, int want_down) {
    SceCtrlData pad;
    ctrl_read(&pad);
    u32 now = pad.Buttons;
    u32 changed = edges->primed ? now ^ edges->previous : 0;
    edges->previous = now;
    edges->primed = 1;
    return changed & (want_down ? now : ~now);
}

// pressed(): the buttons that went down since the last call, so each press
// is reported once. The first call returns [], so the X that started the
// script from the launcher doesn't count.
static mp_obj_t psp_pressed(void) {
    return button_list(button_changes(&pressed_edges, 1));
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_pressed_obj, psp_pressed);

// released(): the buttons that came up since the last call, so each release
// is reported once. The first call returns [].
static mp_obj_t psp_released(void) {
    return button_list(button_changes(&released_edges, 0));
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_released_obj, psp_released);

// The analog stick's dead zone, set with set_deadzone(). A resting stick
// reads up to about 10 off centre on a PSP-1000, and more as sticks wear.
#define DEADZONE_DEFAULT 16
static int deadzone = DEADZONE_DEFAULT;

// One stick axis, 0..255 from the hardware, to -127..127. Within the dead
// zone it's 0; beyond it the rest of the range is stretched back to 127, so
// there's no jump at the dead zone's edge.
static mp_int_t stick_axis(unsigned char raw) {
    mp_int_t v = (mp_int_t)raw - 128;
    mp_int_t mag = v < 0 ? -v : v;
    if (mag <= deadzone) {
        return 0;
    }
    mag = (mag - deadzone) * 127 / (127 - deadzone);
    if (mag > 127) {
        mag = 127;
    }
    return v < 0 ? -mag : mag;
}

// analog(): the stick as (x, y), each -127..127 with 0 at the centre;
// x grows to the right and y grows downwards. Readings inside the dead zone
// (see set_deadzone) are 0.
static mp_obj_t psp_analog(void) {
    SceCtrlData pad;
    ctrl_read(&pad);
    mp_obj_t xy[2] = {
        MP_OBJ_NEW_SMALL_INT(stick_axis(pad.Lx)),
        MP_OBJ_NEW_SMALL_INT(stick_axis(pad.Ly)),
    };
    return mp_obj_new_tuple(2, xy);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_analog_obj, psp_analog);

// set_deadzone(n): how far from centre, 0-126, the stick must move before
// analog() reads anything but 0. The default is 16; 0 gives raw readings.
// Each script run from the launcher starts with the default.
static mp_obj_t psp_set_deadzone(mp_obj_t n_in) {
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 0 || n > 126) {
        mp_raise_ValueError(MP_ERROR_TEXT("deadzone must be 0-126"));
    }
    deadzone = n;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(psp_set_deadzone_obj, psp_set_deadzone);

// Put the input state back as a new script expects it: pressed() and
// released() forget what they last saw (so their next calls count as first
// calls) and the dead zone returns to the default. The launcher calls this
// (through _launcher) before each script, as scripts share one MicroPython
// session and so this state.
void psp_buttons_reset(void) {
    pressed_edges.primed = 0;
    released_edges.primed = 0;
    deadzone = DEADZONE_DEFAULT;
}

// vsync(): wait for the start of the next display refresh (60 Hz).
static mp_obj_t psp_vsync(void) {
    sceDisplayWaitVblankStart();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_vsync_obj, psp_vsync);

// battery(): charge in percent (0-100), or None with no battery fitted.
static mp_obj_t psp_battery(void) {
    if (!scePowerIsBatteryExist()) {
        return mp_const_none;
    }
    int percent = scePowerGetBatteryLifePercent();
    return percent < 0 ? mp_const_none : MP_OBJ_NEW_SMALL_INT(percent);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_battery_obj, psp_battery);

// battery_minutes(): estimated minutes of battery left, or None if unknown
// (no battery, or on mains power).
static mp_obj_t psp_battery_minutes(void) {
    if (!scePowerIsBatteryExist()) {
        return mp_const_none;
    }
    int minutes = scePowerGetBatteryLifeTime();
    return minutes < 0 ? mp_const_none : MP_OBJ_NEW_SMALL_INT(minutes);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_battery_minutes_obj, psp_battery_minutes);

// charging(): True while the battery is charging.
static mp_obj_t psp_charging(void) {
    return mp_obj_new_bool(scePowerIsBatteryCharging() == 1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_charging_obj, psp_charging);

// on_ac(): True when running from the mains adapter.
static mp_obj_t psp_on_ac(void) {
    return mp_obj_new_bool(scePowerIsPowerOnline() == 1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_on_ac_obj, psp_on_ac);

// freq(): the clocks as (cpu, bus) in MHz.
// freq(cpu): set the CPU clock, 19-333 MHz, with the bus at half of it.
// MicroPython starts at 333 MHz; lower clocks save battery.
static mp_obj_t psp_freq(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        mp_int_t cpu = mp_obj_get_int(args[0]);
        if (cpu < 19 || cpu > 333) {
            mp_raise_ValueError(MP_ERROR_TEXT("cpu freq must be 19-333 MHz"));
        }
        int ret = scePowerSetClockFrequency(cpu, cpu, cpu / 2);
        if (ret < 0) {
            mp_raise_OSError(MP_EINVAL);
        }
        return mp_const_none;
    }
    mp_obj_t freqs[2] = {
        MP_OBJ_NEW_SMALL_INT(scePowerGetCpuClockFrequencyInt()),
        MP_OBJ_NEW_SMALL_INT(scePowerGetBusClockFrequencyInt()),
    };
    return mp_obj_new_tuple(2, freqs);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(psp_freq_obj, 0, 1, psp_freq);

// emulator(): True when running in PPSSPP rather than on a real PSP.
static mp_obj_t psp_emulator(void) {
    return mp_obj_new_bool(psp_emu_is_emulator());
}
static MP_DEFINE_CONST_FUN_OBJ_0(psp_emulator_obj, psp_emulator);

static const mp_rom_map_elem_t psp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_psp) },
    { MP_ROM_QSTR(MP_QSTR_held), MP_ROM_PTR(&psp_held_obj) },
    { MP_ROM_QSTR(MP_QSTR_pressed), MP_ROM_PTR(&psp_pressed_obj) },
    { MP_ROM_QSTR(MP_QSTR_released), MP_ROM_PTR(&psp_released_obj) },
    { MP_ROM_QSTR(MP_QSTR_analog), MP_ROM_PTR(&psp_analog_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_deadzone), MP_ROM_PTR(&psp_set_deadzone_obj) },
    { MP_ROM_QSTR(MP_QSTR_vsync), MP_ROM_PTR(&psp_vsync_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery), MP_ROM_PTR(&psp_battery_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery_minutes), MP_ROM_PTR(&psp_battery_minutes_obj) },
    { MP_ROM_QSTR(MP_QSTR_charging), MP_ROM_PTR(&psp_charging_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_ac), MP_ROM_PTR(&psp_on_ac_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&psp_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_emulator), MP_ROM_PTR(&psp_emulator_obj) },

    // Button names, as held(), pressed() and released() return them: psp.CROSS is "CROSS".
    { MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_QSTR(MP_QSTR_UP) },
    { MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_QSTR(MP_QSTR_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_QSTR(MP_QSTR_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_QSTR(MP_QSTR_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_CROSS), MP_ROM_QSTR(MP_QSTR_CROSS) },
    { MP_ROM_QSTR(MP_QSTR_CIRCLE), MP_ROM_QSTR(MP_QSTR_CIRCLE) },
    { MP_ROM_QSTR(MP_QSTR_TRIANGLE), MP_ROM_QSTR(MP_QSTR_TRIANGLE) },
    { MP_ROM_QSTR(MP_QSTR_SQUARE), MP_ROM_QSTR(MP_QSTR_SQUARE) },
    { MP_ROM_QSTR(MP_QSTR_L), MP_ROM_QSTR(MP_QSTR_L) },
    { MP_ROM_QSTR(MP_QSTR_R), MP_ROM_QSTR(MP_QSTR_R) },
    { MP_ROM_QSTR(MP_QSTR_START), MP_ROM_QSTR(MP_QSTR_START) },
    { MP_ROM_QSTR(MP_QSTR_SELECT), MP_ROM_QSTR(MP_QSTR_SELECT) },
};
static MP_DEFINE_CONST_DICT(psp_module_globals, psp_module_globals_table);

const mp_obj_module_t psp_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&psp_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_psp, psp_module);
