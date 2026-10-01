// Console I/O for the PSP port.
//
// stdout goes to the PSP's stdout (fd 1), which PSPLINK and the PPSSPP log
// both capture, and to the on-screen debug console unless pspdisplay has
// the screen. Under PPSSPPHeadless it
// also goes to the emulator's own stdout, which doesn't echo fd 1.
#include <string.h>

#include <pspdebug.h>
#include <pspiofilemgr.h>

#include "py/mphal.h"
#include "py/stream.h"
#include "psp_display.h"
#include "psp_emu.h"

static int headless = 0;

void mp_hal_init(void) {
    headless = psp_emu_is_headless();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    sceIoWrite(1, str, len);
    // While a script draws with pspdisplay, the screen is its own.
    if (!psp_display_active()) {
        pspDebugScreenPrintData(str, len);
    }
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

// For polling sys.stdin/stdout (select.poll). Output is always ready. There's
// no non-blocking way to check stdin, so report it as never readable.
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    return poll_flags & MP_STREAM_POLL_WR;
}

// Seed for random, used at first import and by random.seed() with no
// argument. The microsecond timer depends on how long the user took to get
// there; the counter makes back-to-back calls differ. Mixed with murmur3's
// finaliser so nearby times give unrelated seeds.
uint32_t psp_random_seed(void) {
    static uint32_t calls;
    uint64_t t = sceKernelGetSystemTimeWide();
    uint32_t x = (uint32_t)t ^ (uint32_t)(t >> 32) ^ (++calls * 0x9e3779b9u);
    x ^= (uint32_t)(mp_hal_time_ns() / 1000000000ULL);
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}
