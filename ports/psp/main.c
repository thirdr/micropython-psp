// MicroPython entry point for the Sony PSP.
//
// Sets up the PSP (clock, exit callback, debug screen), starts MicroPython
// with a GC heap from malloc, mounts the filesystem, and runs boot.py then
// main.py from the EBOOT's folder (the working directory). Without a main.py
// it runs the launcher, or the selftest in test builds. Under PPSSPPHeadless
// it captures the screen and exits by itself; in the PPSSPP GUI or on
// hardware it waits for HOME -> Exit.
//
// Started from PSPLINK's shell as "./micropython.prx repl", it runs MicroPython's
// REPL over the USB link instead, where Ctrl-D soft-resets.
#include <stdlib.h>
#include <string.h>

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
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "psp_display.h"

#include "psp_emu.h"

// MICROPY_PSP_MAIN_STACK_KB and MICROPY_PSP_GC_HEAP_KB come from CMakeLists.txt.
PSP_MODULE_INFO("MicroPython", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(MICROPY_PSP_MAIN_STACK_KB);
// Give newlib all of user memory except 1 MB, for the GC heap and C code.
PSP_HEAP_SIZE_KB(-1024);

// What runs when there's no main.py: the launcher, or the selftest in test
// builds (MICROPY_PSP_TEST_BUILD in CMakeLists.txt).
#if MICROPY_PSP_TEST_BUILD
#define PSP_FALLBACK_MODULE "selftest.py"
#else
#define PSP_FALLBACK_MODULE "launcher.py"
#endif

// HOME -> Exit ends the program straight away, even mid-script: waiting for
// a flag would leave HOME dead while the launcher or a script is running.
static int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
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

// Leave straight away under PPSSPPHeadless. Otherwise keep the output on
// screen until HOME -> Exit, which the exit callback handles.
static void MP_NORETURN psp_exit(void) {
    if (psp_emu_is_headless()) {
        sceDisplayWaitVblankStart();
        psp_emu_screenshot();
        sceKernelExitGame();
    }
    for (;;) {
        sceKernelSleepThread();
    }
}

// Mount newlib's filesystem at "/" and make it the current VFS, so relative
// paths resolve against the working directory. As in ports/unix: don't use
// chdir("/"), which would change the VfsPosix object's own path.
static void mount_filesystem(void) {
    mp_obj_t args[2] = {
        MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
        MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
    };
    mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
    MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    while (MP_STATE_VM(vfs_cur)->next != NULL) {
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_cur)->next;
    }
}

// picovector's text() markup registry belongs to the frozen _markup module,
// and picovector doesn't keep it alive itself, so hold the module for good.
MP_REGISTER_ROOT_POINTER(mp_obj_t psp_markup_module);

static void import_markup(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        MP_STATE_PORT(psp_markup_module) = mp_import_name(MP_QSTR__markup, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    }
}

void psp_audio_reset(void);
void psp_buttons_reset(void);
void psp_network_reset(void);

// The REPL is for development, over PSPLINK: "./micropython.prx repl" in
// pspsh (PSPLINK starts the build's PRX; it can't start an EBOOT.PBP).
// Test builds also take it from a file standing in for the keyboard, since
// PPSSPPHeadless has no stdin and can't pass arguments.
#define PSP_REPL_TEST_INPUT "repl-input.txt"

static bool want_repl(int argc, char *argv[], const char **feed) {
    *feed = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "repl") == 0) {
            return true;
        }
    }
    #if MICROPY_PSP_TEST_BUILD
    SceIoStat st;
    if (sceIoGetstat(PSP_REPL_TEST_INPUT, &st) >= 0) {
        *feed = PSP_REPL_TEST_INPUT;
        return true;
    }
    #endif
    return false;
}

// Returns when the REPL asks for a soft reset (Ctrl-D, or sys.exit()).
static void run_repl(void) {
    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }
}

// What the launcher does after each script, before a soft reset frees the
// heap: nothing may still point into it.
static void reset_hardware(void) {
    psp_display_release();
    psp_audio_reset();
    psp_network_reset();
    psp_buttons_reset();
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

    const char *feed;
    bool repl = want_repl(argc, argv, &feed);
    if (repl) {
        psp_stdin_start(feed);
    }

    for (;;) {
        gc_init(heap, heap + heap_size);
        mp_init();
        mount_filesystem();
        // sys.path is ['', '.frozen'] by default; add the EBOOT folder's lib/.
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_lib));
        import_markup();

        pyexec_file_if_exists("boot.py");
        if (!repl) {
            if (mp_import_stat("main.py") == MP_IMPORT_STAT_FILE) {
                pyexec_file("main.py");
            } else {
                pyexec_frozen_module(PSP_FALLBACK_MODULE, false);
            }
            break;
        }
        run_repl();
        if (psp_stdin_eof()) {
            break;
        }
        reset_hardware();
        mp_deinit();
        mp_hal_stdout_tx_str("MPY: soft reboot\r\n");
    }
    mp_deinit();

    psp_exit();
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

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
