# picovector for the PSP: Pimoroni's PicoVector (v3) and its MicroPython
# bindings, compiled unmodified from the picovector-micropython submodule,
# plus pspdisplay, which puts a picovector image on the PSP screen.
#
# Include this from the port's CMakeLists.txt after add_executable() and
# before mkrules.cmake, which needs MICROPY_SOURCE_QSTR complete.
set(PICOVECTOR_PSP_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PICOVECTOR_MICROPYTHON_DIR ${PSP_REPO_DIR}/picovector-micropython)

# The portable rasteriser: the hardware interpolator is the RP2's. (Rasterising
# on a second core, PV_DUAL_CORE, is off by default.)
set(PV_HARDWARE_INTERP OFF)

# picovector-micropython is a MicroPython user C module: MicroPython's
# usermod.cmake includes it and gathers its sources and include folders.
set(USER_C_MODULES ${PICOVECTOR_MICROPYTHON_DIR}/picovector_micropython.cmake)
include(${MICROPY_DIR}/py/usermod.cmake)

target_link_libraries(${MICROPY_TARGET} PRIVATE usermod)

# pngdec.cmake gives its one C++ file a C-only warning flag
# (-Wno-deprecated-non-prototype), which g++ warns about; keep the rest.
set_source_files_properties(${PICOVECTOR_MICROPYTHON_DIR}/picovector/lib/pngdec/PNGdec.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-error=unused-function"
)

# JPEGDEC.h includes Arduino.h unless it's told it's on a known platform.
# PICO_BUILD only switches that to the C library headers (as on the Pico),
# both where the decoder is built and where its header is included.
target_compile_definitions(jpegdec PRIVATE PICO_BUILD)
target_compile_definitions(${MICROPY_TARGET} PRIVATE PICO_BUILD)

# picovector's only use of PICO is an fx16_vec2_t(int, int) constructor for
# where int32_t is long, as it is on the PSP too.
target_compile_definitions(${MICROPY_TARGET} PRIVATE PICO=1)

set(PICOVECTOR_PSP_SOURCES
    ${PICOVECTOR_PSP_DIR}/pspdisplay.c
    ${PICOVECTOR_PSP_DIR}/picovector_psp_compat.c
)

target_sources(${MICROPY_TARGET} PRIVATE ${PICOVECTOR_PSP_SOURCES})

list(APPEND MICROPY_SOURCE_QSTR ${MICROPY_SOURCE_USERMOD} ${PICOVECTOR_PSP_SOURCES})

# On the target itself too, so the qstr preprocessor (mkrules.cmake) sees them.
target_include_directories(${MICROPY_TARGET} PRIVATE
    ${PICOVECTOR_PSP_DIR}
    ${MICROPY_INC_USERMOD}
)

target_compile_options(${MICROPY_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics>
    # m_malloc_no_scan(), which upstream MicroPython lacks.
    $<$<COMPILE_LANGUAGE:CXX>:-include picovector_psp_compat.h>
)
