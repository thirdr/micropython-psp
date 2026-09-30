// Port-specific parts of the time module, included by extmod/modtime.c.
#include "py/obj.h"
#include "py/mphal.h"

// time.time(): seconds since boot, until there's an RTC-backed clock.
static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int_from_uint(mp_hal_ticks_ms() / 1000);
}
