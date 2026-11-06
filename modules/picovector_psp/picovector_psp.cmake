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

# pico3d's depth buffer is picovector's working buffer, and a surface bigger
# than it is drawn in bands that don't depth-test against each other's
# render() calls. The badge sizes it to fit its SRAM; the PSP has the room for
# 512x320 (the biggest surface the GE takes), so any such surface is one band
# on the CPU fallback too, as it is on the GE.
get_target_property(PV_DEFINITIONS usermod_picovector INTERFACE_COMPILE_DEFINITIONS)
list(TRANSFORM PV_DEFINITIONS REPLACE "^PV_WORKING_BUFFER_SIZE=.*" "PV_WORKING_BUFFER_SIZE=327680")
set_target_properties(usermod_picovector PROPERTIES INTERFACE_COMPILE_DEFINITIONS "${PV_DEFINITIONS}")

set(PICOVECTOR_PSP_SOURCES
    ${PICOVECTOR_PSP_DIR}/pspdisplay.c
    ${PICOVECTOR_PSP_DIR}/picovector_psp_compat.c
    ${PICOVECTOR_PSP_DIR}/pico3d_ge.cpp
)

target_sources(${MICROPY_TARGET} PRIVATE ${PICOVECTOR_PSP_SOURCES})

# pico3d on the GE (pico3d_ge.cpp): the bindings' calls into pico3d's draw,
# scene and depth-clear functions, and the scene constructor, go to its
# wrappers first. pspgu goes before the port's pspge, which it needs.
target_link_libraries(${MICROPY_TARGET} PRIVATE pspgu)
target_link_options(${MICROPY_TARGET} PRIVATE
    -Wl,--wrap=_ZN10picovector16pico3d_draw_meshEPNS_15pico3d_target_tEPKNS_13pico3d_mesh_tEPKNS_6mat4_tES7_PKNS_17pico3d_material_tENS_16pico3d_shading_tEPKNS_14pico3d_light_tEPNS_15pico3d_vcache_tES7_
    -Wl,--wrap=_ZN10picovector16pico3d_scene_addEPNS_14pico3d_scene_tEPKNS_15pico3d_target_tEPKNS_13pico3d_mesh_tEPKNS_6mat4_tESA_PKNS_17pico3d_material_tENS_16pico3d_shading_tEPKNS_14pico3d_light_tESA_PPKcb
    -Wl,--wrap=_ZN10picovector17pico3d_scene_drawEPNS_14pico3d_scene_tEPNS_15pico3d_target_tEit
    -Wl,--wrap=_ZN10picovector18pico3d_depth_clearEPNS_15pico3d_target_tEt
    -Wl,--wrap=scene_make_new_impl
)

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
