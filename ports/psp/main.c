// MicroPython entry point for the Sony PSP.
//
// Phase 1: sets up the PSP (clock, exit callback, debug screen), starts
// MicroPython with a GC heap from malloc, and runs the frozen main.py. Under
// PPSSPPHeadless it captures the screen and exits by itself; in the PPSSPP GUI
// or on hardware it waits for HOME -> Exit.
#include <stdlib.h>

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <psppower.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

#include "psp_emu.h"

// MICROPY_PSP_MAIN_STACK_KB and MICROPY_PSP_GC_HEAP_KB come from CMakeLists.txt.
PSP_MODULE_INFO("MicroPython", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(MICROPY_PSP_MAIN_STACK_KB);
// Give newlib all of user memory except 1 MB, for the GC heap and C code.
PSP_HEAP_SIZE_KB(-1024);

static volatile int exit_requested = 0;

static int exit_callback(int arg1, int arg2, void *common) {
    exit_requested = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("exit_callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
}

// Wait for HOME -> Exit, or leave straight away under PPSSPPHeadless.
static void MP_NORETURN psp_exit(void) {
    if (psp_emu_is_headless()) {
        sceDisplayWaitVblankStart();
        psp_emu_screenshot();
    } else {
        while (!exit_requested) {
            sceDisplayWaitVblankStart();
        }
    }
    sceKernelExitGame();
    for (;;) {
    }
}

int main(int argc, char *argv[]) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    mp_hal_init();

    mp_cstack_init_with_sp_here(MICROPY_PSP_MAIN_STACK_KB * 1024);

    size_t heap_size = MICROPY_PSP_GC_HEAP_KB * 1024;
    char *heap = malloc(heap_size);
    if (heap == NULL) {
        mp_hal_stdout_tx_str("FATAL: can't allocate the GC heap\n");
        psp_exit();
    }
    gc_init(heap, heap + heap_size);

    mp_init();
    pyexec_frozen_module("main.py", false);
    mp_deinit();

    psp_exit();
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}

// No filesystem until Phase 2, but the io module needs open() to exist.
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

void nlr_jump_fail(void *val) {
    mp_hal_stdout_tx_str("FATAL: uncaught NLR\n");
    psp_exit();
}

void MP_NORETURN __fatal_error(const char *msg) {
    mp_hal_stdout_tx_str("FATAL: ");
    mp_hal_stdout_tx_str(msg);
    mp_hal_stdout_tx_str("\n");
    psp_exit();
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    mp_printf(&mp_plat_print, "Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif
