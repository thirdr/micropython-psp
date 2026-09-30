// Phase 0 hello world: checks the toolchain, clocking, output and exit path.
//
// Prints to the debug screen and to stdout. Under PPSSPPHeadless it captures
// the screen and exits by itself; in the PPSSPP GUI or on hardware it waits
// for HOME -> Exit.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>

#include "psp_emu.h"

PSP_MODULE_INFO("hello", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

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

// printf to stdout, and to PPSSPPHeadless's stdout when running headless.
static void out_printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) {
        return;
    }
    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    sceIoWrite(1, buf, len);
    if (psp_emu_is_headless()) {
        psp_emu_send_output(buf, len);
    }
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
}

int main(int argc, char *argv[]) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    pspDebugScreenInit();
    pspDebugScreenPrintf("Hello from micropython-psp (Phase 0)\n\n");
    pspDebugScreenPrintf("CPU: %d MHz  Bus: %d MHz\n", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
    pspDebugScreenPrintf("Emulator: %s\n", psp_emu_is_emulator() ? "yes" : "no");
    pspDebugScreenPrintf("\nPress HOME to exit.\n");

    out_printf("hello: ok\n");
    out_printf("hello: cpu=%d bus=%d\n", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());

    if (psp_emu_is_headless()) {
        sceDisplayWaitVblankStart();
        psp_emu_screenshot();
        sceKernelExitGame();
        return 0;
    }

    while (!exit_requested) {
        sceDisplayWaitVblankStart();
    }
    sceKernelExitGame();
    return 0;
}
