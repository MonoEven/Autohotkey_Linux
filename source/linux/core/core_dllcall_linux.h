// Declarations of the Linux DllCall support layer (implemented in
// core_dllcall_linux.cpp): dlopen/dlsym equivalents of the Windows
// LoadLibrary/GetProcAddress, and a libffi-based DynaCall that calls an
// arbitrary function pointer with the argument/return types described by
// the DYNAPARM array (same layout as upstream lib/DllCall.cpp).
#pragma once

#include "../../stdafx.h"
#include "../../script.h"

// DllArgTypes is defined in script.h (same enum as upstream lib/DllCall.cpp).
// DYNAPARM/DYNARESULT come from upstream lib/DllCall.cpp, which is not
// compiled on Linux, so define them here.

struct DYNAPARM
{
    union
	{
		int value_int;        // Args whose width is less than 32-bit are also put in here.
		float value_float;
		__int64 value_int64;
		UINT_PTR value_uintptr;
		double value_double;
		char *astr;
		wchar_t *wstr;
		void *ptr;
    };
	DllArgTypes type;
	bool passed_by_address;
	bool is_unsigned;
	bool is_hresult;
};

union DYNARESULT
{
    int     Int;
    long    Long;
    void   *Pointer;
    float   Float;
    double  Double;
    __int64 Int64;
	UINT_PTR UIntPtr;
};

// Calling-convention / return-value flags (upstream values; stdcall and
// cdecl are the same ABI on Linux).
#define  DC_MICROSOFT           0x0000
#define  DC_BORLAND             0x0001
#define  DC_CALL_CDECL          0x0010
#define  DC_CALL_STD            0x0020
#define  DC_RETVAL_MATH4        0x0100
#define  DC_RETVAL_MATH8        0x0200

// Load a shared object.  aName is the Windows-style "dll" name (may be a
// path, with or without a .so/.dll extension, or empty = the process
// itself / RTLD_DEFAULT).  Returns the handle, or nullptr on failure.
void *LinuxDlLoad(const wchar_t *aName, bool aRequired);

// Resolve a symbol in a loaded object (or nullptr to search all loaded
// objects / the process).  Returns the function address or nullptr.
void *LinuxDlSym(void *aHandle, const char *aSymbol);

// Last dl error as a static string (dlerror()).
const char *LinuxDlError();

// Call aFunction with the arguments described by aParam (upstream
// DYNAPARM layout) and return the value in aResult.  aCallMode carries
// the DC_CALL_* / DC_RETVAL_* flags.  Returns true on success.
bool LinuxDynaCall(void *aFunction, DYNAPARM aParam[], int aParamCount
	, int aCallMode, DYNARESULT &aResult);

// Translate a wide string to an AStr (UTF-8) or WStr (UTF-16LE) argument
// buffer and set aParam.ptr.
void LinuxDllCallPrepareStr(DYNAPARM &aParam, const wchar_t *aWide, bool aIsWide);
