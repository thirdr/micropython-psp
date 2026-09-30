# MicroPython port for the Sony PSP, built out of tree against the
# micropython/ submodule.
#
#   psp-cmake -S ports/psp -B build/psp -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/psp
#
# Test build (adds the frozen selftest, run when there's no main.py):
#
#   psp-cmake -S ports/psp -B build/psp-test -DCMAKE_BUILD_TYPE=Release -DMICROPY_PSP_TEST_BUILD=ON
cmake_minimum_required(VERSION 3.16)

project(micropython C ASM)

set(MICROPY_TARGET micropython)
set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(PSP_REPO_DIR ${MICROPY_PORT_DIR}/../.. ABSOLUTE)
set(MICROPY_DIR ${PSP_REPO_DIR}/micropython)

# Memory. The defaults are sized for a PSP-1000 (about 24 MB for homebrew).
# PPSSPP may give more RAM than that, so it won't catch over-allocation.
set(MICROPY_PSP_GC_HEAP_KB 12288 CACHE STRING "GC heap size in KB")
set(MICROPY_PSP_MAIN_STACK_KB 256 CACHE STRING "Main thread stack size in KB")

option(MICROPY_PSP_TEST_BUILD "Freeze the selftest and run it when there's no main.py" OFF)

# A plain 0/1 rather than a generator expression: mkrules.cmake passes the
# compile definitions to the qstr preprocessor as they're written.
if(MICROPY_PSP_TEST_BUILD)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_PORT_DIR}/manifest_test.py)
    set(PSP_TEST_BUILD_VALUE 1)
else()
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_PORT_DIR}/manifest.py)
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
    ${MICROPY_DIR}/shared/runtime/pyexec.c
)

set(MICROPY_SOURCE_PORT
    ${MICROPY_PORT_DIR}/main.c
    ${MICROPY_PORT_DIR}/mphalport.c
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
    # extmod/modvfs.c includes the FAT headers even though FAT is off.
    FFCONF_H=\"${MICROPY_OOFATFS_DIR}/ffconf.h\"
)

target_compile_options(${MICROPY_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    -Wall
    -fsingle-precision-constant
    -Wdouble-promotion
)

target_link_libraries(${MICROPY_TARGET} PRIVATE
    pspdebug
    pspdisplay
    pspge
    psppower
    m
)

include(${MICROPY_DIR}/py/mkrules.cmake)

create_pbp_file(
    TARGET ${MICROPY_TARGET}
    TITLE "MicroPython"
)
