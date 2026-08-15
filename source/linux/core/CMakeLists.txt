# Optional target for porting the AutoHotkey core interpreter to Linux.
#
# This target is intentionally small at first.  Source files are added one by
# one as they are made compatible with the Linux shim.

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
    core_platform_stubs.cpp
    main_linux.cpp
)

target_include_directories(ahk_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat
)

target_compile_options(ahk_core PRIVATE -fpermissive -Wno-jump-misses-init)