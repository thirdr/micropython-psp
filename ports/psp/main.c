// MicroPython entry point for the Sony PSP.
//
// Sets up the PSP (clock, exit callback, debug screen), starts MicroPython
// with a GC heap from malloc, mounts the filesystem, and runs boot.py then
// main.py from the EBOOT's folder (the working directory). Without a main.py
// it runs the launcher, or the selftest in test builds. Under PPSSPPHeadless
// it captures the screen and exits by itself; in the PPSSPP GUI or on
// hardware it waits for HOME -> Exit.
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
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

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
    mount_filesystem();
    // sys.path is ['', '.frozen'] by default; add the EBOOT folder's lib/.
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_lib));

    pyexec_file_if_exists("boot.py");
    if (mp_import_stat("main.py") == MP_IMPORT_STAT_FILE) {
        pyexec_file("main.py");
    } else {
        pyexec_frozen_module(PSP_FALLBACK_MODULE, false);
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
