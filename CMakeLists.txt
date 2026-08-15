# AutoHotkey Linux migration build system (initial scaffold).
#
# This CMake project currently only provides the platform abstraction library.
# It does not yet compile the full AutoHotkey interpreter because the upstream
# source still depends on Win32 headers.  See LINUX_PORT.md for the roadmap.

cmake_minimum_required(VERSION 3.16)
project(AutoHotkey_Linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_AHK_CORE "Build AutoHotkey core interpreter for Linux" OFF)

add_subdirectory(source/linux)

if(BUILD_AHK_CORE)
    add_subdirectory(source/linux/core)
endif()
