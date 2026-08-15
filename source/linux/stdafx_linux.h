#pragma once

// Linux compatibility header for AutoHotkey source.
//
// This is a first-pass shim that provides the Win32 types/macros most commonly
// used by AutoHotkey's core source.  It intentionally does NOT implement the
// full Windows API; platform-specific modules (GUI/hooks/clipboard/etc.) will
// be ported later via PlatformAbstraction.h.
//
// This header is included from source/stdafx.h when compiling on Linux.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstdarg>
#include <ctime>
#include <alloca.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Base integer types
// ---------------------------------------------------------------------------
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef unsigned int       DWORD;
typedef unsigned long long QWORD;

typedef int                BOOL;
typedef int                INT;
typedef unsigned int       UINT;
typedef long               LONG;
typedef unsigned long      ULONG;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;

typedef intptr_t           INT_PTR;
typedef uintptr_t          UINT_PTR;
typedef intptr_t           LONG_PTR;
typedef uintptr_t          ULONG_PTR;
typedef size_t             SIZE_T;
typedef ptrdiff_t          SSIZE_T;

typedef char               CHAR;
typedef wchar_t            WCHAR;
typedef float              FLOAT;

#ifndef __int64
typedef long long          __int64;
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// ---------------------------------------------------------------------------
// Handles / pointers
// ---------------------------------------------------------------------------
typedef void*              HANDLE;
typedef void*              HINSTANCE;
typedef void*              HMODULE;
typedef void*              HWND;
typedef void*              HKEY;
typedef void*              HDC;
typedef void*              HBRUSH;
typedef void*              HICON;
typedef void*              HCURSOR;
typedef void*              HMENU;
typedef void*              HHOOK;
typedef void*              HKL;
typedef void*              HGDIOBJ;
typedef void*              HFONT;
typedef void*              HBITMAP;
typedef void*              HPEN;
typedef void*              HRGN;
typedef void*              HRAWINPUT;
typedef void*              HACCEL;

typedef void*              LPVOID;
typedef const void*        LPCVOID;

#ifdef UNICODE
typedef wchar_t            TCHAR;
typedef wchar_t            TBYTE;
typedef wchar_t*           LPTSTR;
typedef const wchar_t*     LPCTSTR;
typedef wchar_t*           LPWSTR;
typedef const wchar_t*     LPCWSTR;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
#else
typedef char               TCHAR;
typedef unsigned char      TBYTE;
typedef char*              LPTSTR;
typedef const char*        LPCTSTR;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
typedef wchar_t*           LPWSTR;
typedef const wchar_t*     LPCWSTR;
#endif

typedef UINT_PTR           WPARAM;
typedef LONG_PTR           LPARAM;
typedef LONG_PTR           LRESULT;
typedef long               HRESULT;

// ---------------------------------------------------------------------------
// Calling conventions / macros
// ---------------------------------------------------------------------------
#define WINAPI
#define CALLBACK
#define APIENTRY
#define STDMETHODCALLTYPE

#ifdef UNICODE
#define __T(x) L##x
#else
#define __T(x) x
#endif
#define _T(x) __T(x)
#define TEXT(x) __T(x)

#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#ifdef UNICODE
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#else
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif

#define LOWORD(l)           ((WORD)(((ULONG_PTR)(l)) & 0xffff))
#define HIWORD(l)           ((WORD)((((ULONG_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w)           ((BYTE)(((ULONG_PTR)(w)) & 0xff))
#define HIBYTE(w)           ((BYTE)((((ULONG_PTR)(w)) >> 8) & 0xff))
#define MAKEWORD(a,b)       ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))
#define MAKELONG(a,b)       ((LONG)(((WORD)(a)) | (((DWORD)((WORD)(b))) << 16)))
#define MAKELPARAM(l,h)     ((LPARAM)(DWORD)MAKELONG(l,h))
#define MAKEWPARAM(l,h)     ((WPARAM)(DWORD)MAKELONG(l,h))

// ---------------------------------------------------------------------------
// String helpers commonly used by the source
// ---------------------------------------------------------------------------
#ifdef UNICODE
#define _tcslen     wcslen
#define _tcsclen    wcslen
#define _tcscpy     wcscpy
#define _tcsncpy    wcsncpy
#define _tcscat     wcscat
#define _tcsncat    wcsncat
#define _tcscmp     wcscmp
#define _tcsncmp    wcsncmp
#define _tcsicmp    wcsicasecmp
#define _tcsnicmp   wcsnicasecmp
#define _tcsstr     wcsstr
#define _tcsrchr    wcsrchr
#define _tcschr     wcschr
#define _ttoi       wcstol
#define _ttoi64     wcstoll
#define _tcstol     wcstol
#define _tcstoul    wcstoul
#define _stprintf   swprintf
#define _sntprintf  swprintf
#define _ftprintf   fwprintf
#define _tprintf    wprintf
#define _tcsftime   wcsftime
#else
#define _tcslen     strlen
#define _tcsclen    strlen
#define _tcscpy     strcpy
#define _tcsncpy    strncpy
#define _tcscat     strcat
#define _tcsncat    strncat
#define _tcscmp     strcmp
#define _tcsncmp    strncmp
#define _tcsicmp    strcasecmp
#define _tcsnicmp   strncasecmp
#define _tcsstr     strstr
#define _tcsrchr    strrchr
#define _tcschr     strchr
#define _ttoi       atoi
#define _ttoi64     atoll
#define _tcstol     strtol
#define _tcstoul    strtoul
#define _stprintf   sprintf
#define _sntprintf  snprintf
#define _ftprintf   fprintf
#define _tprintf    printf
#define _tcsftime   strftime
#endif

inline int wcsicasecmp(const wchar_t* a, const wchar_t* b)
{
	for (; *a && *b; ++a, ++b)
	{
		wchar_t ca = towlower(*a), cb = towlower(*b);
		if (ca != cb)
			return ca < cb ? -1 : 1;
	}
	return (*a == *b) ? 0 : (*a ? 1 : -1);
}

inline int wcsnicasecmp(const wchar_t* a, const wchar_t* b, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		if (!a[i] || !b[i])
			return a[i] == b[i] ? 0 : (a[i] ? 1 : -1);
		wchar_t ca = towlower(a[i]), cb = towlower(b[i]);
		if (ca != cb)
			return ca < cb ? -1 : 1;
	}
	return 0;
}

inline int _isctype(int c, int type)
{
	// Minimal implementation: classify ASCII only.
	if (type & 0x100) return isalpha(c); // _ALPHA
	if (type & 0x200) return isdigit(c); // _DIGIT
	if (type & 0x400) return isspace(c); // _SPACE
	if (type & 0x800) return ispunct(c); // _PUNCT
	if (type & 0x1000) return iscntrl(c); // _CNTRL
	if (type & 0x2000) return isxdigit(c); // _HEX
	return 0;
}

#define _istspace(c) iswspace(c)
#define _istalpha(c) iswalpha(c)
#define _istdigit(c) iswdigit(c)
#define _istalnum(c) iswalnum(c)
#define _istxdigit(c) iswxdigit(c)
#define _istpunct(c) iswpunct(c)
#define _istprint(c) iswprint(c)
#define _istcntrl(c) iswcntrl(c)
#define _istupper(c) iswupper(c)
#define _istlower(c) iswlower(c)

#define IsCharAlpha(c) iswalpha((wchar_t)(c))
#define CharLower(c) towlower((wchar_t)(c))
#define CharUpper(c) towupper((wchar_t)(c))

// ---------------------------------------------------------------------------
// Simple Win32 runtime helpers
// ---------------------------------------------------------------------------
inline void Sleep(DWORD ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, nullptr);
}

inline DWORD GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000L);
}

inline DWORD GetTickCount64()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000L);
}

#define _alloca alloca

// Code page constants used by TextIO.
#define CP_ACP      0
#define CP_OEMCP    1
#define CP_UTF8     65001
#define CP_UTF16    1200
#define CP_UTF16BE  1201

// Placeholder for codepage info; expanded as needed.
struct CPINFO
{
	UINT MaxCharSize;
	BYTE LeadByte[12];
};

inline BOOL GetCPInfo(UINT, CPINFO* lpCPInfo)
{
	if (lpCPInfo)
	{
		lpCPInfo->MaxCharSize = 1;
		memset(lpCPInfo->LeadByte, 0, sizeof(lpCPInfo->LeadByte));
	}
	return TRUE;
}

// Avoid pulling in the real windows.h on non-Windows.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
