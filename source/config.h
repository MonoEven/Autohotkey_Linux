#pragma once
// This file defines some macros for compile-time configurations.

// The upstream source uses _WIN64 to select 64-bit struct layouts (pointers are 8 bytes).
// On 64-bit Linux we must enable the same layout for ABI compatibility.
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#ifndef _WIN64
#define _WIN64
#endif
#endif
// (Like many projects on *nix that using autotools.)

#if defined(WIN32) && !defined(_WIN64)
#define WIN32_PLATFORM
#endif

#if defined(_MSC_VER) || defined(__linux__)
	#if defined(WIN32_PLATFORM) || defined(_WIN64) || defined(__linux__)
	#define ENABLE_DLLCALL
	#define ENABLE_REGISTERCALLBACK
	#endif
#endif

#if !defined(_MBCS) && !defined(_UNICODE) && !defined(UNICODE) // If not set in project settings...

// L: Comment out the next line to enable UNICODE:
//#define _MBCS

#ifndef _MBCS
#define _UNICODE
#define UNICODE
#endif
#endif

#ifndef AUTOHOTKEYSC
// DBGp: Linux uses the POSIX compatibility layer and Linux CLI/main-loop.
#define CONFIG_DEBUGGER
#endif

// Generates warnings to help we check whether the codes are ready to handle Unicode or not.
//#define CONFIG_UNICODE_CHECK

// This is now defined via Config.vcxproj if supported by the current platform toolset.
//#ifndef _WIN64
//#define CONFIG_WIN2K
//#endif