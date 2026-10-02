// Console I/O for the PSP port.
//
// stdout goes to the PSP's stdout (fd 1), which PSPLINK and the PPSSPP log
// both capture, and to the on-screen debug console unless pspdisplay has
// the screen. Under PPSSPPHeadless it
// also goes to the emulator's own stdout, which doesn't echo fd 1.
//
// stdin is only there under PSPLINK, where fd 0 is the USB link to the Mac.
#include <string.h>

#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/readline/readline.h"
#include "psp_display.h"
#include "psp_emu.h"

// The debug screen's grid: 68 columns of 7 px, 34 rows of 8 px.
#define SCREEN_COLUMNS 68

static int headless = 0;

void mp_hal_init(void) {
    headless = psp_emu_is_headless();
}

// The REPL's line editing sends VT100 codes: "\b" and ESC [ n D move the
// cursor back, ESC [ K erases to the end of the line. The debug screen would
// print them as glyphs, so act on those, and drop any other ESC [ code and
// control character (raw REPL's Ctrl-Ds).
static void screen_print(const char *str, size_t len) {
    static enum { TEXT, ESC, CSI } state = TEXT;
    static int param;
    size_t run = 0;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (state == TEXT && ((uint8_t)c >= 0x20 || c == '\n' || c == '\r' || c == '\t')) {
            run++;
            continue;
        }
        pspDebugScreenPrintData(str + i - run, run);
        run = 0;
        int x = pspDebugScreenGetX(), y = pspDebugScreenGetY();
        if (state == TEXT) {
            if (c == '\b') {
                pspDebugScreenSetXY(x > 0 ? x - 1 : 0, y);
            } else if (c == 0x1b) {
                state = ESC;
            }
        } else if (state == ESC) {
            state = c == '[' ? CSI : TEXT;
            param = 0;
        } else if (c >= '0' && c <= '9') {
            param = param * 10 + (c - '0');
        } else if (c >= 0x40 && c <= 0x7e) {
            if (c == 'D') {
                x -= param > 0 ? param : 1;
                pspDebugScreenSetXY(x > 0 ? x : 0, y);
            } else if (c == 'K') {
                for (int col = x; col < SCREEN_COLUMNS; col++) {
                    pspDebugScreenPutChar(col * 7, y * 8, 0xffffffff, ' ');
                }
            }
            state = TEXT;
        }
    }
    pspDebugScreenPrintData(str + len - run, run);
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    sceIoWrite(1, str, len);
    // While a script draws with pspdisplay, the screen is its own.
    if (!psp_display_active()) {
        screen_print(str, len);
    }
    if (headless) {
        psp_emu_send_output(str, len);
    }
    return len;
}

// The debug screen handles a bare "\n", but in REPL mode the other end is a
// terminal on the Mac, which needs "\r\n" as on other MicroPython boards.
static bool stdin_threaded;

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    if (!stdin_threaded) {
        mp_hal_stdout_tx_strn(str, len);
        return;
    }
    const char *end = str + len;
    while (str < end) {
        const char *nl = memchr(str, '\n', end - str);
        if (nl == NULL) {
            mp_hal_stdout_tx_strn(str, end - str);
            break;
        }
        mp_hal_stdout_tx_strn(str, nl - str);
        mp_hal_stdout_tx_strn("\r\n", 2);
        str = nl + 1;
    }
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

// REPL mode reads stdin on a thread of its own, into this ring buffer. Under
// PSPLINK a read of fd 0 waits for the Mac to send something, so the thread
// is ready the moment Ctrl-C arrives, even while Python code is running: it
// raises KeyboardInterrupt instead of queueing the character.
#define STDIN_BUFFER 1024
static volatile uint8_t stdin_buffer[STDIN_BUFFER];
static volatile unsigned stdin_head, stdin_tail;
static volatile bool stdin_ended;
static SceUID stdin_fd = 0;
static bool stdin_feed;

static void stdin_put(int c) {
    if (c == mp_interrupt_char) {
        mp_sched_keyboard_interrupt();
        return;
    }
    unsigned next = (stdin_head + 1) % STDIN_BUFFER;
    while (next == stdin_tail) {
        sceKernelDelayThread(10000);
    }
    stdin_buffer[stdin_head] = c;
    stdin_head = next;
}

static int stdin_reader(SceSize args, void *argp) {
    char chunk[64];
    for (;;) {
        int n = sceIoRead(stdin_fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (stdin_feed) {
                stdin_ended = true;
                return 0;
            }
            sceKernelDelayThread(100000);
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (stdin_feed && chunk[i] == 0) {
                sceKernelDelayThread(500000);
            } else {
                stdin_put((uint8_t)chunk[i]);
            }
        }
    }
}

void psp_stdin_start(const char *feed_path) {
    if (feed_path != NULL) {
        stdin_fd = sceIoOpen(feed_path, PSP_O_RDONLY, 0);
        stdin_feed = true;
    }
    // A higher priority (lower number) than the main thread's 0x20, so a
    // Ctrl-C is seen even while Python code keeps the CPU busy.
    SceUID thread = sceKernelCreateThread("stdin", stdin_reader, 0x18, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (thread >= 0 && sceKernelStartThread(thread, 0, NULL) >= 0) {
        stdin_threaded = true;
    }
}

bool psp_stdin_eof(void) {
    return stdin_ended && stdin_head == stdin_tail;
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        if (stdin_threaded) {
            if (stdin_tail != stdin_head) {
                int c = stdin_buffer[stdin_tail];
                stdin_tail = (stdin_tail + 1) % STDIN_BUFFER;
                return c;
            }
            if (psp_stdin_eof()) {
                return CHAR_CTRL_D;
            }
        } else {
            // Outside REPL mode (a script calling input()): read fd 0
            // directly, which without PSPLINK just fails.
            unsigned char c;
            if (sceIoRead(0, &c, 1) == 1) {
                return c;
            }
        }
        mp_event_wait_ms(10);
    }
}

// For polling sys.stdin/stdout (select.poll). Output is always ready. stdin
// is readable when REPL mode's thread has something queued; without it,
// there's no non-blocking way to check, so it never is.
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = poll_flags & MP_STREAM_POLL_WR;
    if ((poll_flags & MP_STREAM_POLL_RD) && stdin_threaded && stdin_tail != stdin_head) {
        ret |= MP_STREAM_POLL_RD;
    }
    return ret;
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
