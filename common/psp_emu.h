// Helpers for PPSSPP's "emulator:" device.
//
// PPSSPP exposes a few devctl commands on "emulator:" that let a program
// detect the emulator, print to the headless runner's stdout, and ask it to
// capture the screen.
// On real hardware the device doesn't exist, so every call fails harmlessly
// and these helpers report "not an emulator" / "has a display".
#ifndef PSP_EMU_H
#define PSP_EMU_H

#include <pspiofilemgr.h>

#define PSP_EMU_DEVCTL_GET_HAS_DISPLAY  0x01
#define PSP_EMU_DEVCTL_SEND_OUTPUT      0x02
#define PSP_EMU_DEVCTL_IS_EMULATOR      0x03
#define PSP_EMU_DEVCTL_EMIT_SCREENSHOT  0x20

static inline int psp_emu_is_emulator(void) {
    unsigned int result = 0;
    if (sceIoDevctl("emulator:", PSP_EMU_DEVCTL_IS_EMULATOR, NULL, 0, &result, sizeof(result)) < 0) {
        return 0;
    }
    return result != 0;
}

// True when running under PPSSPPHeadless (no window, no input).
static inline int psp_emu_is_headless(void) {
    unsigned int has_display = 1;
    if (sceIoDevctl("emulator:", PSP_EMU_DEVCTL_GET_HAS_DISPLAY, NULL, 0, &has_display, sizeof(has_display)) < 0) {
        return 0;
    }
    return has_display == 0;
}

// Write to PPSSPPHeadless's stdout. PPSSPP only logs the program's own
// stdout (fd 1), so the headless runner needs this to show anything.
static inline void psp_emu_send_output(const char *str, unsigned int len) {
    sceIoDevctl("emulator:", PSP_EMU_DEVCTL_SEND_OUTPUT, (void *)str, len, NULL, 0);
}

// Ask PPSSPPHeadless to capture the current framebuffer.
// tools/run-ppsspp.sh turns the capture into a PNG.
static inline void psp_emu_screenshot(void) {
    sceIoDevctl("emulator:", PSP_EMU_DEVCTL_EMIT_SCREENSHOT, NULL, 0, NULL, 0);
}

#endif // PSP_EMU_H
