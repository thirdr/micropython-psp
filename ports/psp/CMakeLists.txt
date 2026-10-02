# MicroPython port for the Sony PSP, built out of tree against the
# micropython/ submodule.
#
#   psp-cmake -S ports/psp -B build/psp -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/psp
#
# Test build (for tests/repl: reads REPL keystrokes from repl-input.txt):
#
#   psp-cmake -S ports/psp -B build/psp-test -DCMAKE_BUILD_TYPE=Release -DMICROPY_PSP_TEST_BUILD=ON
cmake_minimum_required(VERSION 3.16)

project(micropython C CXX ASM)

set(MICROPY_TARGET micropython)
set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(PSP_REPO_DIR ${MICROPY_PORT_DIR}/../.. ABSOLUTE)
set(MICROPY_DIR ${PSP_REPO_DIR}/micropython)

# Memory. The defaults are sized for a PSP-1000 (about 24 MB for homebrew).
# PPSSPP may give more RAM than that, so it won't catch over-allocation.
# The port's own release version (psp.VERSION, the release zip's name).
# MicroPython's version is separate: sys.version.
set(MICROPY_PSP_VERSION "0.1.0")

set(MICROPY_PSP_GC_HEAP_KB 12288 CACHE STRING "GC heap size in KB")
set(MICROPY_PSP_MAIN_STACK_KB 256 CACHE STRING "Main thread stack size in KB")

option(MICROPY_PSP_TEST_BUILD "Take REPL keystrokes from repl-input.txt, for tests/repl" OFF)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_PORT_DIR}/manifest.py)
# A plain 0/1 rather than a generator expression: mkrules.cmake passes the
# compile definitions to the qstr preprocessor as they're written.
if(MICROPY_PSP_TEST_BUILD)
    set(PSP_TEST_BUILD_VALUE 1)
else()
    set(PSP_TEST_BUILD_VALUE 0)
endif()

# makemanifest.py needs a micropython-lib checkout, but nothing here uses it
# yet. Rather than clone that nested submodule, point it at a placeholder in
# the build directory; a require() in the manifest will fail clearly.
if(NOT MICROPY_LIB_DIR)
    set(MICROPY_LIB_DIR ${CMAKE_BINARY_DIR}/no-micropython-lib)
    file(WRITE ${MICROPY_LIB_DIR}/README.md "Placeholder: micropython-lib isn't used by this build.\n")
endif()

include(${MICROPY_DIR}/py/py.cmake)
include(${MICROPY_DIR}/extmod/extmod.cmake)

add_executable(${MICROPY_TARGET})

set(MICROPY_QSTRDEFS_PORT
    ${MICROPY_PORT_DIR}/qstrdefsport.h
)

set(MICROPY_SOURCE_SHARED
    ${MICROPY_DIR}/shared/readline/readline.c
    ${MICROPY_DIR}/shared/runtime/gchelper_generic.c
    ${MICROPY_DIR}/shared/runtime/interrupt_char.c
    ${MICROPY_DIR}/shared/runtime/pyexec.c
    ${MICROPY_DIR}/shared/runtime/sys_stdio_mphal.c
    ${MICROPY_DIR}/shared/timeutils/timeutils.c
)

set(MICROPY_SOURCE_PORT
    ${MICROPY_PORT_DIR}/main.c
    ${MICROPY_PORT_DIR}/modaudio.c
    ${MICROPY_PORT_DIR}/modlauncher.c
    ${MICROPY_PORT_DIR}/modnetwork.c
    ${MICROPY_PORT_DIR}/modpsp.c
    ${MICROPY_PORT_DIR}/mphalport.c
    ${MICROPY_PORT_DIR}/paths.c
    ${MICROPY_PORT_DIR}/psp_console.c
    ${PSP_REPO_DIR}/psp_compat/poll.c
    ${PSP_REPO_DIR}/psp_compat/psp_socket.c
    ${PSP_REPO_DIR}/psp_compat/psp_socket_block.c
    # MicroPython's POSIX socket module, over libcglue's sockets.
    ${MICROPY_DIR}/ports/unix/modsocket.c
)

set(MICROPY_SOURCE_QSTR
    ${MICROPY_SOURCE_PY}
    ${MICROPY_SOURCE_EXTMOD}
    ${MICROPY_SOURCE_SHARED}
    ${MICROPY_SOURCE_PORT}
)

target_sources(${MICROPY_TARGET} PRIVATE
    ${MICROPY_SOURCE_PY}
    ${MICROPY_SOURCE_EXTMOD}
    ${MICROPY_SOURCE_SHARED}
    ${MICROPY_SOURCE_PORT}
)

# The repo root is on the path for MICROPY_PY_TIME_INCLUDEFILE
# ("ports/psp/modtime.c").
target_include_directories(${MICROPY_TARGET} PRIVATE
    ${MICROPY_INC_CORE}
    ${MICROPY_PORT_DIR}
    ${PSP_REPO_DIR}
    ${PSP_REPO_DIR}/common
    ${PSP_REPO_DIR}/psp_compat
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(${MICROPY_TARGET} PRIVATE
    MICROPY_PSP_GC_HEAP_KB=${MICROPY_PSP_GC_HEAP_KB}
    MICROPY_PSP_MAIN_STACK_KB=${MICROPY_PSP_MAIN_STACK_KB}
    MICROPY_PSP_TEST_BUILD=${PSP_TEST_BUILD_VALUE}
    MICROPY_PSP_VERSION="${MICROPY_PSP_VERSION}"
    # extmod/modvfs.c includes the FAT headers even though FAT is off.
    FFCONF_H=\"${MICROPY_OOFATFS_DIR}/ffconf.h\"
)

# The single-precision flags are for our C only: picovector's C++ is
# written for doubles where it uses them.
target_compile_options(${MICROPY_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99 -fsingle-precision-constant -Wdouble-promotion>
    -Wall
)

# picovector: PicoVector's shapes, images and fonts, and pspdisplay, its
# screen.
include(${PSP_REPO_DIR}/modules/picovector_psp/picovector_psp.cmake)

target_link_libraries(${MICROPY_TARGET} PRIVATE
    pspaudio
    pspctrl
    pspdebug
    pspdisplay
    pspdmac
    pspge
    pspmp3
    pspsdk
    pspnet_apctl
    # Each stub library must land in one piece. Otherwise sceNetInetInit
    # (pulled in by libpspsdk) and the rest (pulled in later by libcglue's
    # sockets) end up in two runs with other modules' stubs between them,
    # and the module's import table binds the wrong functions.
    -Wl,--whole-archive pspnet_resolver pspnet_inet -Wl,--no-whole-archive
    pspnet
    pspwlan
    psppower
    psprtc
    psputility
    m
)

# vfs_posix_file.c always defines sys.stdin/stdout/stderr as fd 0/1/2. Rename
# them in that file only, so sys_stdio_mphal.c's versions (which go through
# mp_hal_stdout_tx_strn, and so reach the screen) are the ones sys uses.
set_source_files_properties(${MICROPY_DIR}/extmod/vfs_posix_file.c PROPERTIES
    COMPILE_DEFINITIONS "mp_sys_stdin_obj=vfs_posix_unused_stdin_obj;mp_sys_stdout_obj=vfs_posix_unused_stdout_obj;mp_sys_stderr_obj=vfs_posix_unused_stderr_obj"
)

# modsocket.c needs getaddrinfo(), inet_pton()/inet_ntop() and the IPv6
# address types, which pspdev's headers lack: psp_compat/psp_socket.h.
set_source_files_properties(${MICROPY_DIR}/ports/unix/modsocket.c PROPERTIES
    COMPILE_OPTIONS "-include;psp_socket.h"
)

# paths.c wraps getcwd() (a drive's top reads "umd0:/", not "umd0:") and
# libcglue's __path_absolute() (repeated slashes are collapsed), and rename()
# (moving to another folder fails with EXDEV).
target_link_options(${MICROPY_TARGET} PRIVATE -Wl,--wrap=getcwd -Wl,--wrap=__path_absolute -Wl,--wrap=rename)

# psp_socket_block.c makes blocking sockets block in PPSSPP, which doesn't
# implement them yet (on hardware the wrappers pass straight through).
target_link_options(${MICROPY_TARGET} PRIVATE
    -Wl,--wrap=socket -Wl,--wrap=fcntl -Wl,--wrap=setsockopt
    -Wl,--wrap=recv -Wl,--wrap=recvfrom -Wl,--wrap=send -Wl,--wrap=sendto
    -Wl,--wrap=accept -Wl,--wrap=read -Wl,--wrap=write
)

include(${MICROPY_DIR}/py/mkrules.cmake)

# ICON0.PNG is the XMB icon, drawn by tools/make_icon.py.
# A PRX inside the EBOOT rather than a plain ELF: PSPLINK loads programs as
# modules, which 6.xx firmware only allows for PRXs (0x80020148 otherwise),
# and the XMB and PPSSPP run either.
create_pbp_file(
    TARGET ${MICROPY_TARGET}
    TITLE "MicroPython"
    ICON_PATH ${MICROPY_PORT_DIR}/assets/ICON0.PNG
    BUILD_PRX
)

# For tools/make-release.sh.
file(WRITE ${CMAKE_BINARY_DIR}/VERSION "${MICROPY_PSP_VERSION}\n")
