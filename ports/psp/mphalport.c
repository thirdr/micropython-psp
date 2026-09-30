// Console I/O for the PSP port.
//
// stdout goes to the PSP's stdout (fd 1), which PSPLINK and the PPSSPP log
// both capture, and to the on-screen debug console. Under PPSSPPHeadless it
// also goes to the emulator's own stdout, which doesn't echo fd 1.
#include <string.h>

#include <pspdebug.h>
#include <pspiofilemgr.h>

#include "py/mphal.h"
#include "psp_emu.h"

static int headless = 0;

void mp_hal_init(void) {
    headless = psp_emu_is_headless();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    sceIoWrite(1, str, len);
    pspDebugScreenPrintData(str, len);
    if (headless) {
        psp_emu_send_output(str, len);
    }
    return len;
}

// The PSP's console and the debug screen both handle bare "\n", so there's
// no newline translation.
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

// stdin is only available under PSPLINK. Without it, block quietly.
int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        unsigned char c;
        if (sceIoRead(0, &c, 1) == 1) {
            return c;
        }
        mp_hal_delay_ms(10);
    }
}
