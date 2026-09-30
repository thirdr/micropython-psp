#ifndef MICROPY_INCLUDED_PSP_MPHALPORT_H
#define MICROPY_INCLUDED_PSP_MPHALPORT_H

#include <psptypes.h>
#include <pspkernel.h>
#include <psprtc.h>

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

// RTC ticks count from 0001-01-01 UTC; this is the offset to 1970-01-01.
#define PSP_RTC_EPOCH_1970_SECONDS (62135596800ULL)

// Wall-clock time in nanoseconds since 1970 (UTC), from the RTC.
static inline uint64_t mp_hal_time_ns(void) {
    u64 tick;
    sceRtcGetCurrentTick(&tick);
    uint64_t res = sceRtcGetTickResolution();
    uint64_t secs = tick / res - PSP_RTC_EPOCH_1970_SECONDS;
    return secs * 1000000000ULL + (tick % res) * (1000000000ULL / res);
}

static inline void mp_hal_delay_us(mp_uint_t us) {
    sceKernelDelayThread(us);
}

static inline void mp_hal_delay_ms(mp_uint_t ms) {
    sceKernelDelayThread(ms * 1000);
}

// How select.poll and asyncio wait: sleep the thread rather than spin (the
// default does nothing). At most 10 ms at a time, so pending events are still
// handled promptly; -1 means wait indefinitely.
#define MICROPY_INTERNAL_WFE(TIMEOUT_MS) \
    sceKernelDelayThread(((int)(TIMEOUT_MS) < 0 || (TIMEOUT_MS) > 10 ? 10 : (TIMEOUT_MS)) * 1000)

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
