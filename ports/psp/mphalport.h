#ifndef MICROPY_INCLUDED_PSP_MPHALPORT_H
#define MICROPY_INCLUDED_PSP_MPHALPORT_H

#include <psptypes.h>
#include <pspkernel.h>

#include "py/mpconfig.h"

// Call once at startup, before any output.
void mp_hal_init(void);

static inline mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)sceKernelGetSystemTimeWide();
}

static inline mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(sceKernelGetSystemTimeWide() / 1000);
}

static inline mp_uint_t mp_hal_ticks_cpu(void) {
    return (mp_uint_t)sceKernelGetSystemTimeLow();
}

static inline uint64_t mp_hal_time_ns(void) {
    return (uint64_t)sceKernelGetSystemTimeWide() * 1000;
}

static inline void mp_hal_delay_us(mp_uint_t us) {
    sceKernelDelayThread(us);
}

static inline void mp_hal_delay_ms(mp_uint_t ms) {
    sceKernelDelayThread(ms * 1000);
}

static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

// Used by VfsPosix to retry a file call interrupted with EINTR (PEP 475).
// Taken from ports/unix/mphalport.h.
#include <errno.h>
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            MP_THREAD_GIL_EXIT(); \
            ret = syscall; \
            MP_THREAD_GIL_ENTER(); \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
}

#endif // MICROPY_INCLUDED_PSP_MPHALPORT_H
