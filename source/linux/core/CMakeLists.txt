# Optional target for porting the AutoHotkey core interpreter to Linux.
#
# This target is intentionally small at first.  Source files are added one by
# one as they are made compatible with the Linux shim.

# Bundled PCRE1 (16-bit) used by lib/regex.cpp (RegExMatch/RegExReplace).
set(PCRE16_SOURCES
    ../../lib_pcre/pcre/pcre16_chartables.c
    ../../lib_pcre/pcre/pcre16_compile.c
    ../../lib_pcre/pcre/pcre16_config.c
    ../../lib_pcre/pcre/pcre16_exec.c
    ../../lib_pcre/pcre/pcre16_fullinfo.c
    ../../lib_pcre/pcre/pcre16_get.c
    ../../lib_pcre/pcre/pcre16_globals.c
    ../../lib_pcre/pcre/pcre16_jit_compile.c
    ../../lib_pcre/pcre/pcre16_newline.c
    ../../lib_pcre/pcre/pcre16_ord2utf16.c
    ../../lib_pcre/pcre/pcre16_refcount.c
    ../../lib_pcre/pcre/pcre16_string_utils.c
    ../../lib_pcre/pcre/pcre16_study.c
    ../../lib_pcre/pcre/pcre16_tables.c
    ../../lib_pcre/pcre/pcre16_ucd.c
    ../../lib_pcre/pcre/pcre16_valid_utf16.c
    ../../lib_pcre/pcre/pcre16_version.c
    ../../lib_pcre/pcre/pcre16_xclass.c
)
add_library(ahk_pcre16 STATIC ${PCRE16_SOURCES})
target_compile_options(ahk_pcre16 PRIVATE -w -DHAVE_CONFIG_H)
set_target_properties(ahk_pcre16 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Core interpreter sources that compile on Linux.  They are not linked into
# the minimal runner yet because many built-in functions/GUI symbols still need
# Linux implementations or stubs.
add_library(ahk_core_objects STATIC
    ../../script.cpp
    ../../script2.cpp
    ../../script_expression.cpp
    ../../script_object.cpp
    ../../script_object_bif.cpp
    ../../lib/math.cpp
    ../../lib/string.cpp
    ../../lib/file.cpp
    ../../lib/interop.cpp
    ../../lib/regex.cpp
    ../../error.cpp
    ../../hotkey.cpp
    ../../os_version.cpp
    ../../var.cpp
    ../../window.cpp
)

target_include_directories(ahk_core_objects PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat
)

target_compile_options(ahk_core_objects PRIVATE -fpermissive -Wno-jump-misses-init)

add_executable(ahk_core
    ../../ahkversion.cpp
    ../../SimpleHeap.cpp
    ../../StringConv.cpp
    ../../globaldata.cpp
    ../../TextIO.cpp
    ../../util.cpp
    $<TARGET_OBJECTS:ahk_core_objects>
    core_stubs.cpp
    core_builtin_stubs.cpp
    core_mdfunc_linux.cpp
    core_file_linux.cpp
    core_win_linux.cpp
    core_input_linux.cpp
    core_ctrl_linux.cpp
    core_screen_linux.cpp
    core_display_linux.cpp
    core_platform_stubs.cpp
    main_linux.cpp
    ../gui/x11_gui.cpp
)

target_include_directories(ahk_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat
)

target_compile_options(ahk_core PRIVATE -fpermissive -Wno-jump-misses-init)

find_package(X11 REQUIRED)
find_library(XTST_LIBRARY Xtst REQUIRED)
find_library(XRANDR_LIBRARY Xrandr REQUIRED)
find_library(XINERAMA_LIBRARY Xinerama REQUIRED)
target_link_libraries(ahk_core PRIVATE X11::X11 ahk_pcre16 ${XTST_LIBRARY} ${XRANDR_LIBRARY} ${XINERAMA_LIBRARY})