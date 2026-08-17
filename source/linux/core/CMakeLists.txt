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

# Wayland backend: xdg-shell + virtual keyboard/pointer protocols.  The
# protocol XMLs are vendored in ../wayland/protocols; wayland-scanner
# generates client code at build time.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
find_program(WAYLAND_SCANNER_EXE wayland-scanner REQUIRED)
set(WL_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wayland/protocols")
set(WL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/wl_gen")
file(MAKE_DIRECTORY "${WL_GEN_DIR}")
set(WL_PROTO_NAMES
  xdg-shell
  virtual-keyboard-unstable-v1
  wlr-virtual-pointer-unstable-v1
  wlr-screencopy-unstable-v1
)
set(WL_GEN_SOURCES "")
foreach(p ${WL_PROTO_NAMES})
  add_custom_command(
    OUTPUT "${WL_GEN_DIR}/${p}-protocol.h" "${WL_GEN_DIR}/${p}-protocol.c"
    COMMAND ${WAYLAND_SCANNER_EXE} client-header "${WL_PROTO_DIR}/${p}.xml" "${WL_GEN_DIR}/${p}-protocol.h"
    COMMAND ${WAYLAND_SCANNER_EXE} private-code "${WL_PROTO_DIR}/${p}.xml" "${WL_GEN_DIR}/${p}-protocol.c"
    DEPENDS "${WL_PROTO_DIR}/${p}.xml"
  )
  list(APPEND WL_GEN_SOURCES "${WL_GEN_DIR}/${p}-protocol.c")
endforeach()

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
    core_image_linux.cpp
    core_display_linux.cpp
    core_timer_linux.cpp
    core_hotkey_linux.cpp
    core_clipboard_linux.cpp
    core_dllcall_linux.cpp
    core_com_dbus_linux.cpp
    core_com_bif_linux.cpp
    core_md_func_linux.cpp
    core_platform_stubs.cpp
    core_wayland_linux.cpp
    main_linux.cpp
    ../gui/x11_gui.cpp
    ../gui/script_gui_linux.cpp
    ../gui/script_menu_linux.cpp
    ${WL_GEN_SOURCES}
)

target_include_directories(ahk_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat
    ${WL_GEN_DIR}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
)

target_compile_options(ahk_core PRIVATE -fpermissive -Wno-jump-misses-init)

find_package(X11 REQUIRED)
find_library(XTST_LIBRARY Xtst REQUIRED)
find_library(XRANDR_LIBRARY Xrandr REQUIRED)
find_library(XINERAMA_LIBRARY Xinerama REQUIRED)
find_library(XEXT_LIBRARY Xext REQUIRED)
# DllCall on Linux: dlopen/dlsym (libdl is part of glibc on modern
# systems, but link explicitly for portability) + libffi for the call.
find_library(DL_LIBRARY dl REQUIRED)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
# GTK3 GUI backend (GuiType/GuiControlType/UserMenu).
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(ahk_core PRIVATE X11::X11 ahk_pcre16 ${XTST_LIBRARY} ${XRANDR_LIBRARY} ${XINERAMA_LIBRARY} ${XEXT_LIBRARY} PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON ${DL_LIBRARY} PkgConfig::LIBFFI PkgConfig::DBUS PkgConfig::GTK3)