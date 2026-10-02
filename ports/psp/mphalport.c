// Console I/O for the PSP port.
//
// stdout goes to the PSP's stdout (fd 1), which PSPLINK and the PPSSPP log
// both capture, and to the console on screen (psp_console.c) unless
// pspdisplay has the screen. Under PPSSPPHeadless it also goes to the
// emulator's own stdout, which doesn't echo fd 1.
//
// stdin is only there under PSPLINK, where fd 0 is the USB link to the Mac.
#include <string.h>

#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/readline/readline.h"
#include "psp_display.h"
#include "psp_emu.h"
#include "psp_port.h"

static int headless = 0;

void mp_hal_init(void) {
    headless = psp_emu_is_headless();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    sceIoWrite(1, str, len);
    // While a script draws with pspdisplay, the screen is its own.
    if (!psp_display_active()) {
        psp_console_write(str, len);
    }
    if (headless) {
        psp_emu_send_output(str, len);
    }
    return len;
}

// The console on screen handles a bare "\n", but in REPL mode the other end
// is a terminal on the Mac, which needs "\r\n" as on other MicroPython
// boards.
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
        int n = sceIoRead(0, chunk, sizeof(chunk));
        for (int i = 0; i < n; i++) {
            stdin_put((uint8_t)chunk[i]);
        }
        if (n <= 0) {
            sceKernelDelayThread(100000);
        }
    }
    return 0;
}

#if MICROPY_PSP_TEST_BUILD
// Test builds: a file stands in for the keyboard, as PPSSPPHeadless has no
// stdin. A 0 byte pauses half a second, so a Ctrl-C can arrive mid-loop, and
// the end of the file ends the REPL (psp_stdin_eof()).
static SceUID feed_fd;
static volatile bool feed_ended;

static int feed_reader(SceSize args, void *argp) {
    char c;
    while (sceIoRead(feed_fd, &c, 1) == 1) {
        if (c == 0) {
            sceKernelDelayThread(500000);
        } else {
            stdin_put((uint8_t)c);
        }
    }
    feed_ended = true;
    return 0;
}
#endif

void psp_stdin_start(const char *feed_path) {
    SceKernelThreadEntry reader = stdin_reader;
    #if MICROPY_PSP_TEST_BUILD
    if (feed_path != NULL) {
        feed_fd = sceIoOpen(feed_path, PSP_O_RDONLY, 0);
        reader = feed_reader;
    }
    #else
    (void)feed_path;
    #endif
    // A higher priority (lower number) than the main thread's 0x20, so a
    // Ctrl-C is seen even while Python code keeps the CPU busy.
    SceUID thread = sceKernelCreateThread("stdin", reader, 0x18, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (thread >= 0 && sceKernelStartThread(thread, 0, NULL) >= 0) {
        stdin_threaded = true;
    }
}

bool psp_stdin_eof(void) {
    #if MICROPY_PSP_TEST_BUILD
    return feed_ended && stdin_head == stdin_tail;
    #else
    return false;
    #endif
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
