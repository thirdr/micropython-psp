// MicroPython configuration for the Sony PSP.
//
// Extra features (most of the standard modules), plus a filesystem: scripts
// load from the EBOOT's folder (the working directory). No REPL yet.
#include <stdint.h>
#include <alloca.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// Board identification.
#define MICROPY_HW_BOARD_NAME                   "Sony PSP"
#define MICROPY_HW_MCU_NAME                     "Allegrex"
#define MICROPY_PY_SYS_PLATFORM                 "psp"

// Compiler and code execution. There's no native emitter for MIPS, so the
// bytecode VM only.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_PERSISTENT_CODE_LOAD            (1)
// compile(), so the launcher's tracebacks name the script.
#define MICROPY_PY_BUILTINS_COMPILE             (1)

// Exceptions and GC. Start with the portable setjmp paths; py/nlrmips.c
// assumes the o32 ABI and needs checking against the PSP's ABI first.
#define MICROPY_NLR_SETJMP                      (1)
#define MICROPY_GCREGS_SETJMP                   (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_ENABLE_FINALISER                (1)
// m_tracked_calloc()/m_tracked_free(): heap blocks only C code points to
// (picovector's PNG and JPEG decoders), kept alive until freed, as on rp2.
#define MICROPY_TRACKED_ALLOC                   (1)
// socket: ports/unix/modsocket.c over libcglue (see CMakeLists.txt).
#define MICROPY_PY_SOCKET                       (1)
// Keeps exception messages when the heap is locked or full, as ports/unix does.
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF  (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE    (256)
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_STACK_CHECK_MARGIN              (8 * 1024)

// The Allegrex FPU is single precision only.
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)
// newlib's powf gives nan for pow(nan, 0) and pow(1, nan), and tgammaf(-inf)
// gives inf; these make math follow CPython.
#define MICROPY_PY_MATH_POW_FIX_NAN             (1)
#define MICROPY_PY_MATH_GAMMA_FIX_NEGINF        (1)
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)

// Error reporting and help.
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ENABLE_SOURCE_LINE              (1)
#define MICROPY_HELPER_REPL                     (1)

// Use newlib's printf rather than MicroPython's own.
#define MICROPY_USE_INTERNAL_PRINTF             (0)

// Filesystem. VfsPosix passes file operations through to newlib, which
// maps them onto the PSP's own file calls (ms0:/, host0:/, umd0:/).
#define MICROPY_VFS                             (1)
#define MICROPY_VFS_POSIX                       (1)
#define MICROPY_READER_VFS                      (1)

// Modules.
#define MICROPY_MODULE_FROZEN_MPY               (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)
#define MICROPY_PY_SYS_PATH                     (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_INCLUDEFILE             "ports/psp/modtime.c"
#define MICROPY_PY_OS                           (1)
// deflate compression as well as decompression; size isn't tight here.
#define MICROPY_PY_DEFLATE_COMPRESS             (1)
// sys.stdin/stdout/stderr come from shared/runtime/sys_stdio_mphal.c, so they
// go through mp_hal_stdout_tx_strn (screen and headless output as well as
// fd 1). CMakeLists.txt renames VfsPosix's own fd-based versions out of the way.

// Time: a 1970 epoch as in CPython, 64-bit timestamps.
#define MICROPY_EPOCH_IS_1970                   (1)
#define MICROPY_TIMESTAMP_IMPL                  (MICROPY_TIMESTAMP_IMPL_LONG_LONG)

// Seed random from the CPU cycle counter at import.
uint32_t psp_random_seed(void);
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (psp_random_seed())

#define MICROPY_ALLOC_PATH_MAX                  (256)

// Type definitions for the specific machine.
typedef long mp_off_t;
// newlib's limits.h has no SSIZE_MAX.
#define MP_SSIZE_MAX (0x7fffffff)

#define MP_STATE_PORT MP_STATE_VM
