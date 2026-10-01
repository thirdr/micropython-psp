// Force-included into the C++ (picovector) sources.
//
// picovector-micropython is written against Pimoroni's MicroPython fork,
// which adds m_malloc_no_scan(): GC blocks the collector doesn't scan, for
// pure pixel data. Upstream MicroPython doesn't have it, so
// picovector_psp_compat.c gives an ordinary m_malloc() instead: the same
// memory, just scanned.
#ifndef PICOVECTOR_PSP_COMPAT_H
#define PICOVECTOR_PSP_COMPAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *m_malloc_no_scan(size_t num_bytes);

#ifdef __cplusplus
}
#endif

#endif // PICOVECTOR_PSP_COMPAT_H
