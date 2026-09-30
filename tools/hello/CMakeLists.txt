cmake_minimum_required(VERSION 3.16)

project(hello C)

add_executable(hello main.c)
target_include_directories(hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_compile_options(hello PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(hello PRIVATE pspdebug pspdisplay pspge psppower)

create_pbp_file(
    TARGET hello
    TITLE "MicroPython PSP hello"
)
