# Linux platform abstraction layer.
#
# This is a header-only interface target for now.  X11/Wayland backend
# implementations will be added as separate source files in later stages.

add_library(ahk_linux_platform INTERFACE)
target_include_directories(ahk_linux_platform INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)
