# Linux platform abstraction layer.
#
# This target exposes the platform-abstraction headers.  The X11/Wayland
# backends themselves are real, working implementations (core_*.cpp is
# compiled into ahk_core by the top-level CMakeLists); this INTERFACE
# target is kept for header include paths and compatibility.  New source
# files are not automatically added here -- the top-level ahk_core target
# collects them.

add_library(ahk_linux_platform INTERFACE)
target_include_directories(ahk_linux_platform INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)