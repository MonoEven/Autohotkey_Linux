# Optional target for porting the AutoHotkey core interpreter to Linux.
#
# This target is intentionally small at first.  Source files are added one by
# one as they are made compatible with the Linux shim.

add_executable(ahk_core
    ../../ahkversion.cpp
    ../../SimpleHeap.cpp
    ../../StringConv.cpp
    ../../globaldata.cpp
    ../../TextIO.cpp
    core_stubs.cpp
    main_linux.cpp
)

target_include_directories(ahk_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../compat
)

# AutoHotkey upstream uses several MSVC extensions; allow them while porting.
target_compile_options(ahk_core PRIVATE -fpermissive)
