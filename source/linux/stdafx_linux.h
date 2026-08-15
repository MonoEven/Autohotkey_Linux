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
#include <string>
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
typedef uint8_t            UINT8;
typedef uint16_t           UINT16;
typedef uint32_t           UINT32;
typedef int8_t             INT8;
typedef int16_t            INT16;
typedef int32_t            INT32;
typedef unsigned char      BYTE;
typedef unsigned char      UCHAR;
typedef unsigned short     WORD;
typedef unsigned short     USHORT;
typedef short              SHORT;
typedef unsigned int       DWORD;
typedef unsigned long long QWORD;

typedef int                BOOL;
typedef unsigned char      BOOLEAN;
typedef int                INT;
typedef unsigned int       UINT;
typedef long               LONG;
typedef unsigned long      ULONG;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef int64_t            INT64;
typedef uint64_t           UINT64;

typedef intptr_t           INT_PTR;
typedef uintptr_t          UINT_PTR;
typedef intptr_t           LONG_PTR;
typedef uintptr_t          ULONG_PTR;
typedef size_t             SIZE_T;
typedef ptrdiff_t          SSIZE_T;

typedef char               CHAR;
typedef wchar_t            WCHAR;
typedef float              FLOAT;
typedef DWORD              COLORREF;

#define RGB(r,g,b)          ((COLORREF)(((BYTE)(r)) | (((WORD)((BYTE)(g))) << 8) | (((DWORD)((BYTE)(b))) << 16)))
#define GetRValue(rgb)      ((BYTE)(rgb))
#define GetGValue(rgb)      ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)      ((BYTE)((rgb) >> 16))

struct FILETIME
{
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
};

struct SYSTEMTIME
{
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
};

struct POINT
{
	LONG x;
	LONG y;
};

struct RECT
{
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
};

struct ENUMLOGFONTEX
{
	// Minimal stub for declarations; fields are not used by the core port yet.
	BYTE reserved[128];
};

struct NEWTEXTMETRICEX
{
	// Minimal stub for declarations; fields are not used by the core port yet.
	BYTE reserved[128];
};

typedef void* HIMAGELIST;
typedef WORD  ATOM;
typedef void* HDROP;

struct ACCEL
{
	BYTE fVirt;
	WORD key;
	WORD cmd;
};
typedef ACCEL* LPACCEL;

struct NMHDR
{
	void* hwndFrom;
	UINT_PTR idFrom;
	UINT code;
};
typedef NMHDR* LPNMHDR;

struct NOTIFYICONDATA
{
	DWORD cbSize;
	void* hWnd;
	UINT uID;
	UINT uFlags;
	UINT uCallbackMessage;
	void* hIcon;
	wchar_t szTip[128];
};
struct PROCESS_INFORMATION
{
	void* hProcess;
	void* hThread;
	DWORD dwProcessId;
	DWORD dwThreadId;
};

union LARGE_INTEGER
{
	struct { DWORD LowPart; LONG HighPart; };
	LONGLONG QuadPart;
};
typedef LARGE_INTEGER* PLARGE_INTEGER;

struct OSVERSIONINFOW
{
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	wchar_t szCSDVersion[128];
};

struct KBDLLHOOKSTRUCT
{
	DWORD vkCode;
	DWORD scanCode;
	DWORD flags;
	DWORD time;
	ULONG_PTR dwExtraInfo;
};

struct MSG
{
	void* hwnd;
	UINT message;
	UINT_PTR wParam;
	LONG_PTR lParam;
	DWORD time;
	POINT pt;
};

struct LOGFONT
{
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	wchar_t lfFaceName[32];
};
struct WIN32_FIND_DATA
{
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	wchar_t cFileName[260];
	wchar_t cAlternateFileName[14];
};

#ifndef __int64
#define __int64 long long
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ZeroMemory(dest, len) memset((dest), 0, (len))

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Common virtual-key codes needed by the core/keyboard headers.
#define VK_LBUTTON        0x01
#define VK_RBUTTON        0x02
#define VK_CANCEL         0x03
#define VK_MBUTTON        0x04
#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_RETURN         0x0D
#define VK_CLEAR          0x0C
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12
#define VK_PAUSE          0x13
#define VK_HELP           0x2F
#define VK_CAPITAL        0x14
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_SNAPSHOT       0x2C
#define VK_0              0x30
#define VK_1              0x31
#define VK_2              0x32
#define VK_3              0x33
#define VK_4              0x34
#define VK_5              0x35
#define VK_6              0x36
#define VK_7              0x37
#define VK_8              0x38
#define VK_9              0x39
#define VK_A              0x41
#define VK_B              0x42
#define VK_C              0x43
#define VK_D              0x44
#define VK_E              0x45
#define VK_F              0x46
#define VK_G              0x47
#define VK_H              0x48
#define VK_I              0x49
#define VK_J              0x4A
#define VK_K              0x4B
#define VK_L              0x4C
#define VK_M              0x4D
#define VK_N              0x4E
#define VK_O              0x4F
#define VK_P              0x50
#define VK_Q              0x51
#define VK_R              0x52
#define VK_S              0x53
#define VK_T              0x54
#define VK_U              0x55
#define VK_V              0x56
#define VK_W              0x57
#define VK_X              0x58
#define VK_Y              0x59
#define VK_Z              0x5A
#define VK_LWIN           0x5B
#define VK_RWIN           0x5C
#define VK_APPS           0x5D
#define VK_SLEEP          0x5F
#define VK_NUMPAD0        0x60
#define VK_NUMPAD1        0x61
#define VK_NUMPAD2        0x62
#define VK_NUMPAD3        0x63
#define VK_NUMPAD4        0x64
#define VK_NUMPAD5        0x65
#define VK_NUMPAD6        0x66
#define VK_NUMPAD7        0x67
#define VK_NUMPAD8        0x68
#define VK_NUMPAD9        0x69
#define VK_MULTIPLY       0x6A
#define VK_ADD            0x6B
#define VK_SEPARATOR      0x6C
#define VK_SUBTRACT       0x6D
#define VK_DECIMAL        0x6E
#define VK_DIVIDE         0x6F
#define VK_F1             0x70
#define VK_F2             0x71
#define VK_F3             0x72
#define VK_F4             0x73
#define VK_F5             0x74
#define VK_F6             0x75
#define VK_F7             0x76
#define VK_F8             0x77
#define VK_F9             0x78
#define VK_F10            0x79
#define VK_F11            0x7A
#define VK_F12            0x7B
#define VK_F13            0x7C
#define VK_F14            0x7D
#define VK_F15            0x7E
#define VK_F16            0x7F
#define VK_F17            0x80
#define VK_F18            0x81
#define VK_F19            0x82
#define VK_F20            0x83
#define VK_F21            0x84
#define VK_F22            0x85
#define VK_F23            0x86
#define VK_F24            0x87
#define VK_NUMLOCK        0x90
#define VK_SCROLL         0x91
#define VK_LSHIFT         0xA0
#define VK_RSHIFT         0xA1
#define VK_LCONTROL       0xA2
#define VK_RCONTROL       0xA3
#define VK_LMENU          0xA4
#define VK_RMENU          0xA5

// Code page constants used by TextIO and string conversion.
#define CP_ACP      0
#define CP_OEMCP    1
#define CP_UTF8     65001
#define CP_UTF7     65000
#define CP_UTF16    1200
#define CP_UTF16BE  1201

// Clipboard / dialog / registry constants needed by headers.
#define CF_TEXT           1
#define CF_BITMAP         2
#define CF_OEMTEXT        7
#define CF_DIB            8
#define CF_UNICODETEXT    13
#define CF_ENHMETAFILE    14
#define CF_DSPENHMETAFILE 0x008E
#define CF_DIBV5          17
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define IDCANCEL 2
#define VOID void
#define ERROR_SUCCESS 0

#define REG_NONE                     0
#define REG_SZ                       1
#define REG_EXPAND_SZ                2
#define REG_BINARY                   3
#define REG_DWORD                    4
#define REG_DWORD_BIG_ENDIAN         5
#define REG_LINK                     6
#define REG_MULTI_SZ                 7
#define REG_RESOURCE_LIST            8
#define REG_FULL_RESOURCE_DESCRIPTOR 9
#define REG_RESOURCE_REQUIREMENTS_LIST 10
#define REG_QWORD                    11
#define KEY_WOW64_32KEY              0x0200
#define KEY_WOW64_64KEY              0x0100

#define SW_HIDE        0
#define SW_SHOWNORMAL  1
#define SW_MAXIMIZE    3
#define SW_MINIMIZE    6

#define CLR_DEFAULT 0xFF000000
#define CLR_INVALID 0xFFFFFFFF

#define WS_POPUP          0x80000000
#define WS_CLIPSIBLINGS   0x04000000
#define WS_CAPTION        0x00C00000
#define WS_SYSMENU        0x00080000
#define WS_MINIMIZEBOX    0x00020000
#define WM_USER           0x0400

// File API constants needed by TextIO.
#define MB_ERR_INVALID_CHARS  0x00000008
#define GENERIC_READ          0x80000000
#define GENERIC_WRITE         0x40000000
#define OPEN_EXISTING         3
#define CREATE_ALWAYS         2
#define OPEN_ALWAYS           4
#define FILE_SHARE_READ       0x00000001
#define FILE_SHARE_WRITE      0x00000002
#define FILE_SHARE_DELETE     0x00000004
#define STD_OUTPUT_HANDLE     ((DWORD)-11)
#define STD_ERROR_HANDLE      ((DWORD)-12)
#define STD_INPUT_HANDLE      ((DWORD)-10)
#define ERROR_INVALID_HANDLE  6
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_CURRENT          1
#define LOGPIXELSX            88
#define GMEM_MOVEABLE        0x0002
#define GMEM_ZEROINIT        0x0040

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
typedef void*              HGLOBAL;
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

struct CRITICAL_SECTION
{
	void* DebugInfo;
	LONG LockCount;
	LONG RecursionCount;
	void* OwningThread;
	void* LockSemaphore;
	ULONG_PTR SpinCount;
};

typedef long (*WNDPROC)(HWND, UINT, UINT_PTR, LONG_PTR);

typedef void*              LPVOID;
typedef const void*        LPCVOID;
typedef void*              PVOID;
typedef char*              PSTR;
typedef wchar_t*           PWSTR;
typedef BYTE*              LPBYTE;

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

#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR   1
#define FACILITY_WIN32   7
#define MAKE_HRESULT(sev,fac,code) ((HRESULT)(((ULONG)(sev) << 31) | ((ULONG)(fac) << 16) | (ULONG)(code)))

// ---------------------------------------------------------------------------
// Calling conventions / macros
// ---------------------------------------------------------------------------
#define WINAPI
#define CALLBACK
#define APIENTRY
#define STDMETHODCALLTYPE
#define DECLSPEC_NOVTABLE
#define __stdcall
#define __declspec(x)
#define _countof(arr) (sizeof(arr) / sizeof((arr)[0]))

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
#define lstrcmpi    _tcsicmp
#define _tcsnicmp   wcsnicasecmp
#define _tcsstr     wcsstr
#define _tcsrchr    wcsrchr
#define _tcschr     wcschr
#define _ttoi       wcstol
#define _ttoi64     wcstoll
#define _tcstol     wcstol
#define _tcstoul    wcstoul
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
#define lstrcmpi    _tcsicmp
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

inline float _tstof(const wchar_t* s)
{
	return wcstof(s, nullptr);
}
inline float _tstof(const char* s)
{
	return strtof(s, nullptr);
}

#ifdef UNICODE
inline int _stprintf(LPTSTR aBuf, LPCWSTR aFmt, ...)
{
	va_list ap;
	va_start(ap, aFmt);
	int result = vswprintf(aBuf, 32768, aFmt, ap);
	va_end(ap);
	return result;
}
#else
#define _stprintf sprintf
#endif

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

inline TBYTE CharLower(LPTSTR s)
{
	return (TBYTE)towlower((wchar_t)(UINT_PTR)s);
}
inline TBYTE CharLower(TBYTE c)
{
	return (TBYTE)towlower((wchar_t)c);
}
inline TBYTE CharUpper(LPTSTR s)
{
	return (TBYTE)towupper((wchar_t)(UINT_PTR)s);
}
inline TBYTE CharUpper(TBYTE c)
{
	return (TBYTE)towupper((wchar_t)c);
}

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

inline DWORD GetLastError()
{
	return 0;
}

inline BOOL SetEndOfFile(HANDLE)
{
	// Stub: file truncation will be implemented with POSIX ftruncate later.
	return TRUE;
}

inline HWND GetForegroundWindow()
{
	return nullptr;
}

inline HKL GetKeyboardLayout(DWORD)
{
	return nullptr;
}

inline BOOL IsValidCodePage(UINT)
{
	return TRUE;
}

inline int GetDlgCtrlID(HWND)
{
	return 0;
}

inline HWND GetParent(HWND)
{
	return nullptr;
}

inline DWORD GetCurrentThreadId()
{
	return 1;
}

inline HDC GetDC(HWND)
{
	return nullptr;
}

inline int GetDeviceCaps(HDC, int)
{
	return 96;
}

inline int ReleaseDC(HWND, HDC)
{
	return 1;
}

inline int MulDiv(int nNumber, int nNumerator, int nDenominator)
{
	return (int)((long long)nNumber * nNumerator / nDenominator);
}

inline LONG RegConnectRegistry(LPCWSTR, HKEY, HKEY*)
{
	return ERROR_SUCCESS;
}

inline void SetLastError(DWORD)
{
}

inline DWORD GetFileType(HANDLE)
{
	return 1; // FILE_TYPE_DISK
}

inline HANDLE GetStdHandle(DWORD aStdHandle)
{
	if (aStdHandle == STD_OUTPUT_HANDLE)
		return (HANDLE)stdout;
	if (aStdHandle == STD_ERROR_HANDLE)
		return (HANDLE)stderr;
	if (aStdHandle == STD_INPUT_HANDLE)
		return (HANDLE)stdin;
	return nullptr;
}

inline HANDLE CreateFile(LPCTSTR aFileName, DWORD aAccess, DWORD, void*, DWORD aCreation, DWORD, HANDLE)
{
	if (!aFileName)
		return INVALID_HANDLE_VALUE;
	char path[4096];
	size_t converted = wcstombs(path, aFileName, sizeof(path) - 1);
	if (converted == (size_t)-1)
		return INVALID_HANDLE_VALUE;
	path[converted] = '\0';

	bool write = (aAccess & GENERIC_WRITE) != 0;
	bool read = (aAccess & GENERIC_READ) != 0;
	const char* mode = "rb";
	if (aCreation == CREATE_ALWAYS)
		mode = write ? "wb" : "wb";
	else if (aCreation == OPEN_ALWAYS)
		mode = write ? (read ? "a+b" : "ab") : "rb";
	else if (aCreation == OPEN_EXISTING)
		mode = write ? (read ? "r+b" : "rb") : "rb";
	else
		mode = write ? "wb" : "rb";

	FILE* f = fopen(path, mode);
	return (HANDLE)f;
}

inline BOOL CloseHandle(HANDLE hFile)
{
	if (hFile && hFile != INVALID_HANDLE_VALUE)
		return fclose((FILE*)hFile) == 0;
	return TRUE;
}

inline BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD* lpNumberOfBytesRead, void*)
{
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
		return FALSE;
	size_t n = fread(lpBuffer, 1, nNumberOfBytesToRead, (FILE*)hFile);
	if (lpNumberOfBytesRead)
		*lpNumberOfBytesRead = (DWORD)n;
	return n > 0 || nNumberOfBytesToRead == 0;
}

inline BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, DWORD* lpNumberOfBytesWritten, void*)
{
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
		return FALSE;
	size_t n = fwrite(lpBuffer, 1, nNumberOfBytesToWrite, (FILE*)hFile);
	if (lpNumberOfBytesWritten)
		*lpNumberOfBytesWritten = (DWORD)n;
	return n == nNumberOfBytesToWrite;
}

inline BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
		return FALSE;
	int origin = SEEK_SET;
	if (dwMoveMethod == FILE_CURRENT)
		origin = SEEK_CUR;
	else if (dwMoveMethod == 2)
		origin = SEEK_END;
	if (fseek((FILE*)hFile, (long)liDistanceToMove.QuadPart, origin) != 0)
		return FALSE;
	if (lpNewFilePointer)
		lpNewFilePointer->QuadPart = ftell((FILE*)hFile);
	return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER aFileSize)
{
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
		return FALSE;
	long pos = ftell((FILE*)hFile);
	if (fseek((FILE*)hFile, 0, SEEK_END) != 0)
		return FALSE;
	long size = ftell((FILE*)hFile);
	fseek((FILE*)hFile, pos, SEEK_SET);
	if (aFileSize)
		aFileSize->QuadPart = size;
	return TRUE;
}

// Clipboard stubs (will be replaced by X11/Wayland clipboard backend later).
inline UINT EnumClipboardFormats(UINT)
{
	return 0;
}
inline SIZE_T GlobalSize(HANDLE)
{
	return 0;
}
inline LPVOID GlobalLock(HANDLE)
{
	return nullptr;
}
inline BOOL GlobalUnlock(HANDLE)
{
	return FALSE;
}
inline BOOL EmptyClipboard()
{
	return FALSE;
}
inline HANDLE GlobalAlloc(UINT, SIZE_T)
{
	return nullptr;
}
inline HANDLE GlobalFree(HANDLE)
{
	return nullptr;
}
inline HANDLE SetClipboardData(UINT, HANDLE)
{
	return nullptr;
}
inline BOOL OpenClipboard(HWND)
{
	return FALSE;
}
inline BOOL CloseClipboard()
{
	return FALSE;
}
inline HANDLE GetClipboardData(UINT)
{
	return nullptr;
}
inline BOOL IsClipboardFormatAvailable(UINT)
{
	return FALSE;
}

// CRT memory helpers.
inline size_t _msize(void*)
{
	return 0;
}
inline void* _expand(void* p, size_t)
{
	return p;
}
#ifdef UNICODE
#define _tcsnlen wcsnlen
#else
#define _tcsnlen strnlen
#endif

#ifdef UNICODE
inline LPTSTR _itot(int value, LPTSTR buf, int radix) { std::wstring s = std::to_wstring(value); wcscpy(buf, s.c_str()); return buf; }
inline LPTSTR _i64tot(__int64 value, LPTSTR buf, int radix) { std::wstring s = std::to_wstring((long long)value); wcscpy(buf, s.c_str()); return buf; }
inline LPTSTR _ultot(unsigned long value, LPTSTR buf, int radix) { std::wstring s = std::to_wstring(value); wcscpy(buf, s.c_str()); return buf; }
inline LPTSTR _ui64tot(unsigned long long value, LPTSTR buf, int radix) { std::wstring s = std::to_wstring(value); wcscpy(buf, s.c_str()); return buf; }
#else
inline LPTSTR _itot(int value, LPTSTR buf, int radix) { std::string s = std::to_string(value); strcpy(buf, s.c_str()); return buf; }
inline LPTSTR _i64tot(__int64 value, LPTSTR buf, int radix) { std::string s = std::to_string((long long)value); strcpy(buf, s.c_str()); return buf; }
inline LPTSTR _ultot(unsigned long value, LPTSTR buf, int radix) { std::string s = std::to_string(value); strcpy(buf, s.c_str()); return buf; }
inline LPTSTR _ui64tot(unsigned long long value, LPTSTR buf, int radix) { std::string s = std::to_string(value); strcpy(buf, s.c_str()); return buf; }
#endif

inline UINT GetACP()
{
	return CP_UTF8;
}

inline int MultiByteToWideChar(UINT aCodePage, DWORD aFlags, LPCSTR aSrc, int aSrcLen, LPWSTR aDst, int aDstLen)
{
	// Simplified UTF-8/ANSI -> UTF-16 conversion for the Linux port.
	if (!aSrc)
		return 0;
	if (aSrcLen < 0)
		aSrcLen = (int)strlen(aSrc) + 1;
	if (!aDst)
	{
		// Return required buffer size (number of wchar_t units).
		return aSrcLen;
	}
	size_t written = 0;
	for (int i = 0; i < aSrcLen && written < (size_t)aDstLen; ++i)
	{
		unsigned char ch = (unsigned char)aSrc[i];
		if (ch < 0x80)
		{
			aDst[written++] = (wchar_t)ch;
		}
		else if ((ch & 0xE0) == 0xC0 && i + 1 < aSrcLen)
		{
			aDst[written++] = (wchar_t)(((ch & 0x1F) << 6) | (aSrc[++i] & 0x3F));
		}
		else if ((ch & 0xF0) == 0xE0 && i + 2 < aSrcLen)
		{
			aDst[written++] = (wchar_t)(((ch & 0x0F) << 12) | ((aSrc[i + 1] & 0x3F) << 6) | (aSrc[i + 2] & 0x3F));
			i += 2;
		}
		else
		{
			aDst[written++] = (wchar_t)ch;
		}
	}
	return (int)written;
}

inline int WideCharToMultiByte(UINT aCodePage, DWORD aFlags, LPCWSTR aSrc, int aSrcLen, LPSTR aDst, int aDstLen, LPCSTR aDefaultChar, BOOL* aUsedDefaultChar)
{
	// Simplified UTF-16 -> UTF-8/ANSI conversion for the Linux port.
	if (!aSrc)
		return 0;
	if (aSrcLen < 0)
		aSrcLen = (int)wcslen(aSrc) + 1;
	if (!aDst)
	{
		// Rough upper bound: each wchar becomes at most 3 UTF-8 bytes.
		return aSrcLen * 3;
	}
	size_t written = 0;
	for (int i = 0; i < aSrcLen && written + 3 < (size_t)aDstLen; ++i)
	{
		unsigned int cp = (unsigned int)aSrc[i];
		if (cp < 0x80)
		{
			aDst[written++] = (char)cp;
		}
		else if (cp < 0x800)
		{
			aDst[written++] = (char)(0xC0 | (cp >> 6));
			aDst[written++] = (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			aDst[written++] = (char)(0xE0 | (cp >> 12));
			aDst[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			aDst[written++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	return (int)written;
}

#define _alloca alloca
#define _malloca _alloca
#define _freea(p)
#ifdef UNICODE
#define _tcsdup wcsdup
#else
#define _tcsdup strdup
#endif

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
