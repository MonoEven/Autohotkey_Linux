# AutoHotkey Linux migration build system.
#
# With BUILD_AHK_CORE=ON this compiles the core AutoHotkey v2 interpreter for
# Linux (see source/linux/core and LINUX_PORT.md).  The result is a console
# runner that executes .ahk scripts natively; GUI/hotkey backends (X11 etc.)
# are still in progress.

cmake_minimum_required(VERSION 3.16)
project(AutoHotkey_Linux LANGUAGES C CXX) # C for the bundled PCRE1 library.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_AHK_CORE "Build AutoHotkey core interpreter for Linux" ON)

add_subdirectory(source/linux)

if(BUILD_AHK_CORE)
    add_subdirectory(source/linux/core)
endif()
