// Port-specific parts of the time module, included by extmod/modtime.c.
#include <psprtc.h>

#include "py/obj.h"
#include "py/mphal.h"
#include "shared/timeutils/timeutils.h"

// time.time(): UTC seconds since 1970, from the RTC.
static mp_obj_t mp_time_time_get(void) {
    return timeutils_obj_from_timestamp(mp_hal_time_ns() / 1000000000ULL);
}

// time.localtime() / time.gmtime() with no argument: the current local time,
// using the time zone set in the PSP's system settings.
static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    u64 utc, local;
    sceRtcGetCurrentTick(&utc);
    sceRtcConvertUtcToLocalTime(&utc, &local);
    uint64_t seconds = local / sceRtcGetTickResolution() - PSP_RTC_EPOCH_1970_SECONDS;
    timeutils_seconds_since_epoch_to_struct_time(seconds, tm);
}
