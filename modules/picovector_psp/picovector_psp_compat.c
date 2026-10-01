// See picovector_psp_compat.h.
#include "py/misc.h"

#include "picovector_psp_compat.h"

void *m_malloc_no_scan(size_t num_bytes) {
    return m_malloc(num_bytes);
}
