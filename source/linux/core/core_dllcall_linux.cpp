// Linux DllCall support layer.
//
// Full BIF_DllCall/BIF_ComCall implementation for the Linux port.  The
// upstream lib/DllCall.cpp depends on Win32 (LoadLibrary/GetProcAddress,
// SEH, the assembly DynaCall, CStringA translation); on Linux we use:
//   - dlopen/dlsym for shared objects (libfoo.so),
//   - libffi (ffi_call) to invoke functions with arbitrary signatures,
//   - the same argument/return type strings as upstream (Int, UInt, Int64,
//     Str, AStr, WStr, Ptr, Short, Char, Float, Double, ...* by-address,
//     CDecl, HRESULT).
//
// Semantics follow docs-v2 and upstream DllCall.cpp: the first parameter
// is "DllFile\Function" (or a function address), followed by type/value
// pairs; the last parameter, when the count is odd, is the return type
// (default Int).  Output parameters (Str*, Int*, Ptr*, ...) write back to
// the variables after the call.  ComCall dispatches through a vtable
// pointer (COM-like interfaces used by D-Bus etc.).
//
// Differences from Windows (documented):
//   - stdcall/cdecl are the same ABI on Linux; "CDecl" is accepted.
//   - HRESULT semantics are honored (negative -> OSError).
//   - AStr = UTF-8, WStr = UTF-16LE, Str = native wide (UTF-32 on this
//     port, matching upstream's "native string" concept).
//   - A trailing "*" in the type (e.g. "Int*") passes the address and
//     writes the result back to the variable.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_dllcall_linux.h"
#include <dlfcn.h>
#include <ffi.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Type parsing (upstream ConvertDllArgType semantics)
// ---------------------------------------------------------------------------

static void LinuxConvertDllArgType(LPTSTR aBuf, DYNAPARM &aDynaParam)
{
	aDynaParam.is_unsigned = false;
	aDynaParam.passed_by_address = false;
	aDynaParam.is_hresult = false;

	// Work on a modifiable copy: aBuf may point at a read-only literal.
	TCHAR buf[32];
	tcslcpy(buf, aBuf, _countof(buf));
	aBuf = buf;

	// Check for 'U' prefix (unsigned) and '*' suffix (by address).
	if (*aBuf == 'U')
	{
		aDynaParam.is_unsigned = true;
		++aBuf;
	}
	size_t len = _tcslen(aBuf);
	if (len > 0 && aBuf[len - 1] == '*')
	{
		aDynaParam.passed_by_address = true;
		aBuf[--len] = '\0';
	}

	if (!_tcsicmp(aBuf, _T("Int")))
		aDynaParam.type = DLL_ARG_INT;
	else if (!_tcsicmp(aBuf, _T("Int64")))
		aDynaParam.type = DLL_ARG_INT64;
	else if (!_tcsicmp(aBuf, _T("Short")))
		aDynaParam.type = DLL_ARG_SHORT;
	else if (!_tcsicmp(aBuf, _T("Char")))
		aDynaParam.type = DLL_ARG_CHAR;
	else if (!_tcsicmp(aBuf, _T("Float")))
		aDynaParam.type = DLL_ARG_FLOAT;
	else if (!_tcsicmp(aBuf, _T("Double")))
		aDynaParam.type = DLL_ARG_DOUBLE;
	else if (!_tcsicmp(aBuf, _T("Ptr")))
		aDynaParam.type = sizeof(void *) == 8 ? DLL_ARG_INT64 : DLL_ARG_INT;
	else if (!_tcsicmp(aBuf, _T("Str")))
		// On Linux, "Str" means a UTF-8 char* (the native string of .so APIs),
		// equivalent to AStr.  Do NOT use DLL_ARG_STR: under UNICODE it is
		// numerically equal to DLL_ARG_WSTR (UorA), which would duplicate the
		// DLL_ARG_WSTR case in every switch below.
		aDynaParam.type = DLL_ARG_ASTR;
	else if (!_tcsicmp(aBuf, _T("AStr")))
		aDynaParam.type = DLL_ARG_ASTR;
	else if (!_tcsicmp(aBuf, _T("WStr")))
		aDynaParam.type = DLL_ARG_WSTR;
	else
		aDynaParam.type = DLL_ARG_INVALID;
}

// ---------------------------------------------------------------------------
// Shared object loading (dlopen)
// ---------------------------------------------------------------------------

void *LinuxDlLoad(const wchar_t *aName, bool aRequired)
{
	(void)aRequired;
	if (!aName || !*aName)
		return (void *)RTLD_DEFAULT;

	char narrow[4096];
	if (wcstombs(narrow, aName, sizeof(narrow) - 1) == (size_t)-1)
		return nullptr;
	narrow[sizeof(narrow) - 1] = '\0';

	std::string name = narrow;
	// Strip a Windows-style .dll suffix.
	if (name.size() >= 4 && !strcasecmp(name.c_str() + name.size() - 4, ".dll"))
		name.resize(name.size() - 4);

	std::vector<std::string> candidates;
	size_t slash = name.find_last_of('/');
	std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
	std::string dir = (slash == std::string::npos) ? "" : name.substr(0, slash + 1);
	bool has_lib_prefix = base.compare(0, 3, "lib") == 0;
	bool has_so = base.size() >= 3 && !strcmp(base.c_str() + base.size() - 3, ".so");

	if (!has_lib_prefix && !has_so)
		candidates.push_back(dir + "lib" + base + ".so");
	if (!has_so)
		candidates.push_back(name + ".so");
	candidates.push_back(name);

	for (auto &cand : candidates)
	{
		void *h = dlopen(cand.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (h)
			return h;
	}
	return nullptr;
}

void *LinuxDlSym(void *aHandle, const char *aSymbol)
{
	return dlsym(aHandle ? aHandle : RTLD_DEFAULT, aSymbol);
}

const char *LinuxDlError()
{
	return dlerror();
}

// ---------------------------------------------------------------------------
// String translation buffers (alive for the duration of one call)
// ---------------------------------------------------------------------------

namespace {

struct LinuxStrBuf
{
	std::string utf8;
	std::vector<unsigned short> utf16;
};

std::vector<LinuxStrBuf> &LinuxDllCallStrBufs()
{
	static std::vector<LinuxStrBuf> s;
	return s;
}

} // namespace

void LinuxDllCallPrepareStr(DYNAPARM &aParam, const wchar_t *aWide, bool aIsWide)
{
	LinuxStrBuf buf;
	if (aIsWide)
	{
		for (const wchar_t *p = aWide; *p; ++p)
		{
			unsigned int cp = (unsigned int)*p;
			if (cp >= 0x10000)
			{
				cp -= 0x10000;
				buf.utf16.push_back((unsigned short)(0xD800 + (cp >> 10)));
				buf.utf16.push_back((unsigned short)(0xDC00 + (cp & 0x3FF)));
			}
			else
				buf.utf16.push_back((unsigned short)cp);
		}
		buf.utf16.push_back(0);
	}
	else
	{
		for (const wchar_t *p = aWide; *p; ++p)
		{
			unsigned int c = (unsigned int)*p;
			if (c < 0x80) buf.utf8 += (char)c;
			else if (c < 0x800)
			{
				buf.utf8 += (char)(0xC0 | (c >> 6));
				buf.utf8 += (char)(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				buf.utf8 += (char)(0xE0 | (c >> 12));
				buf.utf8 += (char)(0x80 | ((c >> 6) & 0x3F));
				buf.utf8 += (char)(0x80 | (c & 0x3F));
			}
			else
			{
				buf.utf8 += (char)(0xF0 | (c >> 18));
				buf.utf8 += (char)(0x80 | ((c >> 12) & 0x3F));
				buf.utf8 += (char)(0x80 | ((c >> 6) & 0x3F));
				buf.utf8 += (char)(0x80 | (c & 0x3F));
			}
		}
		buf.utf8 += '\0';
	}
	// Push first, then take the pointer: std::string/vector use SSO for
	// short strings, so their internal buffer moves on push_back.
	LinuxDllCallStrBufs().push_back(std::move(buf));
	LinuxStrBuf &stored = LinuxDllCallStrBufs().back();
	aParam.ptr = aIsWide ? (void *)stored.utf16.data() : (void *)stored.utf8.data();
}

// ---------------------------------------------------------------------------
// libffi call
// ---------------------------------------------------------------------------

bool LinuxDynaCall(void *aFunction, DYNAPARM aParam[], int aParamCount
	, int aCallMode, DYNARESULT &aResult)
{
	(void)aCallMode; // stdcall/cdecl are the same ABI on Linux.

	ffi_cif cif;
	ffi_type *arg_types[64];
	void *arg_values[64];
	char ret_space[16] = {0};

	if (aParamCount > 64)
		return false;

	for (int i = 0; i < aParamCount; ++i)
	{
		DYNAPARM &p = aParam[i];
		if (p.passed_by_address)
		{
			// By-address: pass the address of the caller-allocated storage
			// (p.ptr points to it), whatever the element type.
			arg_types[i] = &ffi_type_pointer;
			arg_values[i] = &p.ptr;
			continue;
		}
		switch (p.type)
		{
		// On x86-64 SysV, all integer-like arguments occupy an 8-byte
		// register/stack slot regardless of declared width.  Using 8-byte
		// types ensures that callbacks created via CallbackCreate (which
		// read UINT_PTR-sized values) see the correct argument value.
		// The value_int64 field is 8 bytes, so it safely stores the value.
		case DLL_ARG_INT:   arg_types[i] = &ffi_type_ulong; arg_values[i] = &p.value_int64; break;
		case DLL_ARG_SHORT: arg_types[i] = &ffi_type_ulong; arg_values[i] = &p.value_int64; break;
		case DLL_ARG_CHAR:  arg_types[i] = &ffi_type_ulong; arg_values[i] = &p.value_int64; break;
		case DLL_ARG_INT64: arg_types[i] = &ffi_type_sint64; arg_values[i] = &p.value_int64; break;
		case DLL_ARG_FLOAT: arg_types[i] = &ffi_type_float;  arg_values[i] = &p.value_float; break;
		case DLL_ARG_DOUBLE:arg_types[i] = &ffi_type_double; arg_values[i] = &p.value_double; break;
		default:            arg_types[i] = &ffi_type_pointer;arg_values[i] = &p.ptr; break;
		}
	}

	ffi_type *ret_type;
	if (aCallMode & DC_RETVAL_MATH8)
		ret_type = &ffi_type_double;
	else if (aCallMode & DC_RETVAL_MATH4)
		ret_type = &ffi_type_float;
	else
		ret_type = &ffi_type_ulong; // Integer/pointer (64-bit on LP64).

	if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, aParamCount, ret_type, arg_types) != FFI_OK)
		return false;

	ffi_call(&cif, FFI_FN(aFunction), ret_space, arg_values);

	if (ret_type == &ffi_type_double)
		aResult.Double = *(double *)ret_space;
	else if (ret_type == &ffi_type_float)
		aResult.Float = *(float *)ret_space;
	else
		aResult.UIntPtr = *(UINT_PTR *)ret_space;
	return true;
}

// ---------------------------------------------------------------------------
// BIF_DllCall / BIF_ComCall
// ---------------------------------------------------------------------------

// Shared implementation.  aIsComCall selects the FID_ComCall path
// (parameter 0 = vtable index; parameter 1 = interface pointer + args).
static void LinuxDllCallImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aIsComCall)
{
	void *function = nullptr;
	int vf_index = -1;

	if (aIsComCall)
	{
		if (!ParamIndexIsNumeric(0))
			_f_throw_param(0, _T("Integer"));
		vf_index = (int)ParamIndexToInt64(0);
		if (vf_index < 0)
			_f_throw_param(0);
		// The interface pointer is aParam[1]; it doubles as the first
		// argument ("Ptr").  Normalize like upstream: swap so the rest of
		// the parsing sees type/value pairs starting at aParam[1].
		++aParam;
		--aParamCount;
	}
	else
	{
		// First parameter: "Dll\Function" or a function address (Int) or
		// an object with a Ptr property.
		switch (TypeOfToken(*aParam[0]))
		{
		case SYM_INTEGER:
			function = (void *)ParamIndexToInt64(0);
			break;
		case SYM_STRING:
			function = nullptr; // Resolved below.
			break;
		case SYM_OBJECT:
		{
			__int64 n;
			if (!GetObjectIntProperty(ParamIndexToObject(0), _T("Ptr"), n, aResultToken))
				return;
			function = (void *)n;
			break;
		}
		default:
			_f_throw(ERR_PARAM1_INVALID, ErrorPrototype::Type);
		}
		if (!function)
		{
			// Split "Dll\Function" on the last '\' or '/'.
			TCHAR spec[4096];
			spec[0] = L'\0';
			size_t len = 0;
			LPTSTR spec_str = TokenToString(*aParam[0], spec, &len);
			if (!spec_str)
				spec_str = spec;
			LPTSTR func_part = _tcsrchr(spec_str, '\\');
			if (!func_part)
				func_part = _tcsrchr(spec_str, '/');
			if (!func_part)
			{
				// Whole string = function in the process / loaded objects.
				char narrow[1024];
				if (wcstombs(narrow, spec_str, sizeof(narrow) - 1) == (size_t)-1)
					_f_throw_value(ERR_PARAM1_INVALID);
				narrow[sizeof(narrow) - 1] = '\0';
				function = LinuxDlSym(nullptr, narrow);
				if (!function)
					_f_throw(ERR_NONEXISTENT_FUNCTION, ErrorPrototype::OS);
			}
			else
			{
				*func_part = L'\0';
				++func_part;
				void *hmodule = LinuxDlLoad(spec_str, false);
				if (!hmodule)
				{
					// Throw like upstream: "Failed to load DLL."
					aResultToken.Error(_T("Failed to load DLL: "), spec_str, ErrorPrototype::OS);
					return;
				}
				char narrow[1024];
				if (wcstombs(narrow, func_part, sizeof(narrow) - 1) == (size_t)-1)
					_f_throw_value(ERR_PARAM1_INVALID);
				narrow[sizeof(narrow) - 1] = '\0';
				function = LinuxDlSym(hmodule, narrow);
				if (!function)
				{
					// Try the "A" suffix (upstream behaviour).
					char suffixed[1025];
					strcpy(suffixed, narrow);
					strcat(suffixed, WINAPI_SUFFIX);
					function = LinuxDlSym(hmodule, suffixed);
				}
				if (!function)
					_f_throw(ERR_NONEXISTENT_FUNCTION, ErrorPrototype::OS);
			}
		}
		++aParam;
		--aParamCount;
	}

	// Return type: last parameter when the count is odd.
	DYNAPARM return_attrib = {0};
	int dll_call_mode = DC_CALL_STD;
	if (!(aParamCount % 2))
	{
		return_attrib.type = DLL_ARG_INT;
		if (aIsComCall)
			return_attrib.is_hresult = true;
	}
	else
	{
		ExprTokenType &token = *aParam[aParamCount - 1];
		LPTSTR return_type_string = TokenToString(token);
		if (!_tcsnicmp(return_type_string, _T("CDecl"), 5))
		{
			return_type_string = omit_leading_whitespace(return_type_string + 5);
			if (!*return_type_string)
			{
				return_attrib.type = DLL_ARG_INT;
				goto has_valid_return_type;
			}
		}
		if (!_tcsicmp(return_type_string, _T("HRESULT")))
		{
			return_attrib.type = DLL_ARG_INT;
			return_attrib.is_hresult = true;
		}
		else
			LinuxConvertDllArgType(return_type_string, return_attrib);
		if (return_attrib.type == DLL_ARG_INVALID)
			_f_throw_value(ERR_INVALID_RETURN_TYPE);
	has_valid_return_type:
		--aParamCount;
		if (!return_attrib.passed_by_address)
		{
			if (return_attrib.type == DLL_ARG_DOUBLE)
				dll_call_mode |= DC_RETVAL_MATH8;
			else if (return_attrib.type == DLL_ARG_FLOAT)
				dll_call_mode |= DC_RETVAL_MATH4;
		}
	}

	// Parse the type/value pairs.
	int arg_count = aParamCount / 2;
	DYNAPARM *dyna_param = arg_count ? (DYNAPARM *)malloc(arg_count * sizeof(DYNAPARM)) : nullptr;
	if (arg_count && !dyna_param)
		_f_throw(ERR_OUTOFMEM, ErrorPrototype::OS);
	for (int i = 0; i < arg_count; ++i)
	{
		DYNAPARM &p = dyna_param[i];
		memset(&p, 0, sizeof(p));
		LPTSTR type_str = TokenToString(*aParam[i * 2]);
		LinuxConvertDllArgType(type_str, p);
		if (p.type == DLL_ARG_INVALID)
		{
			free(dyna_param);
			_f_throw_value(ERR_INVALID_ARG_TYPE);
		}

		// "&Var" arguments arrive as VarRef objects; convert to a plain
		// variable reference so output parameters can be written back
		// (upstream does the same in lib/DllCall.cpp).  Use a fresh token
		// (stack allocation) so the caller's argument token is untouched,
		// and rebind val to the fresh token below.
		{
			ExprTokenType &val0 = *aParam[i * 2 + 1];
			if (val0.symbol == SYM_OBJECT)
			{
				IObject *obj = TokenToObject(val0);
				if (obj && dynamic_cast<VarRef *>(obj))
				{
					ExprTokenType *fresh = (ExprTokenType *)_alloca(sizeof(ExprTokenType));
					fresh->SetVarRef(static_cast<VarRef *>(obj));
					aParam[i * 2 + 1] = fresh;
				}
			}
		}
		ExprTokenType &val = *aParam[i * 2 + 1];
		if (val.symbol == SYM_MISSING)
		{
			free(dyna_param);
			_f_throw(ERR_PARAM_REQUIRED);
		}

		// By-address arguments: need writable storage for the value.
		if (p.passed_by_address)
		{
			// Fill the storage with the initial value (where sensible), and
			// always set p.ptr to the storage address (the ffi layer passes
			// &p.ptr, and the write-back reads *p.ptr).  p.is_hresult is
			// reused as an "owns buffer" flag so the cleanup can free only
			// what we allocated.
			p.is_hresult = false;
			switch (p.type)
			{
			case DLL_ARG_INT:
			{
				int *storage = (int *)calloc(1, sizeof(int));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = (int)TokenToInt64(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_INT64:
			{
				__int64 *storage = (__int64 *)calloc(1, sizeof(__int64));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = TokenToInt64(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_SHORT:
			{
				short *storage = (short *)calloc(1, sizeof(short));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = (short)TokenToInt64(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_CHAR:
			{
				char *storage = (char *)calloc(1, sizeof(char));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = (char)TokenToInt64(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_FLOAT:
			{
				float *storage = (float *)calloc(1, sizeof(float));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = (float)TokenToDouble(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_DOUBLE:
			{
				double *storage = (double *)calloc(1, sizeof(double));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = TokenToDouble(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			case DLL_ARG_ASTR:
			case DLL_ARG_WSTR:
				// Str* / AStr* / WStr*: pass a pointer-to-pointer (or a
				// buffer).  Upstream allocates a buffer the function can
				// write into; use the variable's own contents as the
				// initial buffer where possible, else a temp buffer.
				if (val.symbol == SYM_VAR && val.var && val.var->Capacity() > 0)
				{
					p.ptr = val.var->Contents();
					p.value_uintptr = (UINT_PTR)val.var->Contents();
				}
				else
				{
					void *tmp = calloc(1, 4096);
					if (!tmp)
					{
						free(dyna_param);
						_f_throw(ERR_OUTOFMEM, ErrorPrototype::OS);
					}
					p.ptr = tmp;
					p.value_uintptr = (UINT_PTR)tmp;
					p.is_hresult = true;
				}
				break;
			default: // Ptr.
			{
				void **storage = (void **)calloc(1, sizeof(void *));
				if (!storage) { free(dyna_param); _f_throw(ERR_OUTOFMEM, ErrorPrototype::OS); }
				*storage = (void *)(UINT_PTR)TokenToInt64(val);
				p.ptr = storage;
				p.is_hresult = true;
				break;
			}
			}
			// Keep the storage address so we can free and write back.
			p.value_uintptr = (UINT_PTR)p.ptr;
			// Note: for numeric by-address args the value is stored in the
			// allocated block; p.value_int etc. are not used by the ffi path.
			continue;
		}

		switch (p.type)
		{
		case DLL_ARG_ASTR:
		{
			// "Str" and "AStr" both mean UTF-8 on Linux (see LinuxConvertDllArgType).
			if (IS_NUMERIC(val.symbol) || TokenToObject(val))
				_f_throw_type(_T("String"), val);
			TCHAR buf[2]; buf[0] = 0;
			size_t vlen = 0;
			LPTSTR wide = TokenToString(val, buf, &vlen);
			if (!wide) wide = buf;
			LinuxDllCallPrepareStr(p, wide, false);
			break;
		}
		case DLL_ARG_WSTR:
		{
			if (IS_NUMERIC(val.symbol) || TokenToObject(val))
				_f_throw_type(_T("String"), val);
			TCHAR buf[2]; buf[0] = 0;
			size_t vlen = 0;
			LPTSTR wide = TokenToString(val, buf, &vlen);
			if (!wide) wide = buf;
			LinuxDllCallPrepareStr(p, wide, true);
			break;
		}
		case DLL_ARG_DOUBLE:
		case DLL_ARG_FLOAT:
			if (!TokenIsNumeric(val))
				_f_throw_type(_T("Number"), val);
			p.value_double = TokenToDouble(val);
			if (p.type == DLL_ARG_FLOAT)
				p.value_float = (float)p.value_double;
			break;
		default: // INT / INT64 / SHORT / CHAR / PTR.
			if (!TokenIsNumeric(val))
				_f_throw_type(_T("Number"), val);
			p.value_int64 = TokenToInt64(val);
			break;
		}
	}

	// Resolve the vtable function for ComCall.
	if (aIsComCall)
	{
		if ((UINT_PTR)dyna_param[0].ptr < 65536)
		{
			free(dyna_param);
			_f_throw_param(1);
		}
		void **vftbl = *(void ***)dyna_param[0].ptr;
		function = vftbl[vf_index];
	}

	// Call it.
	DYNARESULT return_value;
	memset(&return_value, 0, sizeof(return_value));
	bool ok = LinuxDynaCall(function, dyna_param, arg_count, dll_call_mode, return_value);
	if (!ok)
	{
		// Free per-arg by-address storage (p.is_hresult == "owns buffer").
		for (int i = 0; i < arg_count; ++i)
		{
			DYNAPARM &p = dyna_param[i];
			if (p.passed_by_address && p.is_hresult)
				free(p.ptr);
		}
		free(dyna_param);
		LinuxDllCallStrBufs().clear();
		_f_throw(ERR_PARAM1_INVALID, ErrorPrototype::OS);
	}

	// HRESULT check (docs: "Error values ... are never returned").
	if (return_attrib.is_hresult && FAILED((HRESULT)return_value.Int))
	{
		for (int i = 0; i < arg_count; ++i)
		{
			DYNAPARM &p = dyna_param[i];
			if (p.passed_by_address && p.is_hresult)
				free(p.ptr);
		}
		free(dyna_param);
		LinuxDllCallStrBufs().clear();
		g_script.Win32Error((DWORD)return_value.Int, FAIL);
		aResultToken.SetExitResult(FAIL);
		return;
	}

	// Interpret the return value.
	if (return_attrib.passed_by_address)
	{
		return_attrib.passed_by_address = false;
		switch (return_attrib.type)
		{
		case DLL_ARG_INT64:
		case DLL_ARG_DOUBLE:
			return_value.Int64 = *(__int64 *)return_value.Pointer;
			break;
		default:
			return_value.Int = *(int *)return_value.Pointer;
			break;
		}
	}
	switch (return_attrib.type)
	{
	case DLL_ARG_INT:
		aResultToken.value_int64 = return_attrib.is_unsigned ? (UINT)return_value.Int : return_value.Int;
		break;
	case DLL_ARG_SHORT:
		aResultToken.value_int64 = return_attrib.is_unsigned
			? (return_value.Int & 0x0000FFFF)
			: (short)(unsigned short)return_value.Int;
		break;
	case DLL_ARG_CHAR:
		aResultToken.value_int64 = return_attrib.is_unsigned
			? (return_value.Int & 0x000000FF)
			: (char)(unsigned char)return_value.Int;
		break;
	case DLL_ARG_INT64:
		aResultToken.value_int64 = return_value.Int64;
		break;
	case DLL_ARG_FLOAT:
		aResultToken.symbol = SYM_FLOAT;
		aResultToken.value_double = return_value.Float;
		break;
	case DLL_ARG_DOUBLE:
		aResultToken.symbol = SYM_FLOAT;
		aResultToken.value_double = return_value.Double;
		break;
	case DLL_ARG_ASTR:
	{
		// UTF-8 -> wide.
		const char *s = (const char *)return_value.Pointer;
		std::wstring w;
		if (s)
		{
			size_t n = strlen(s);
			size_t i = 0;
			while (i < n)
			{
				unsigned char c = (unsigned char)s[i];
				unsigned int cp = 0;
				int extra = 0;
				if (c < 0x80) { cp = c; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
				else { ++i; continue; }
				if (i + extra >= n) break;
				bool okc = true;
				for (int k = 1; k <= extra; ++k)
				{
					unsigned char cc = (unsigned char)s[i + k];
					if ((cc & 0xC0) != 0x80) { okc = false; break; }
					cp = (cp << 6) | (cc & 0x3F);
				}
				if (!okc) { ++i; continue; }
				w += (wchar_t)cp;
				i += extra + 1;
			}
		}
		LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
		tmemcpy(persistent, w.c_str(), w.size() + 1);
		aResultToken.symbol = SYM_STRING;
		aResultToken.marker = persistent;
		aResultToken.marker_length = (int)w.size();
		break;
	}
	case DLL_ARG_WSTR:
	{
		// UTF-16LE -> wide.
		const unsigned short *s = (const unsigned short *)return_value.Pointer;
		std::wstring w;
		if (s)
		{
			for (size_t i = 0; s[i]; ++i)
			{
				unsigned int cp = s[i];
				if (cp >= 0xD800 && cp <= 0xDBFF && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
				}
				w += (wchar_t)cp;
			}
		}
		LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
		tmemcpy(persistent, w.c_str(), w.size() + 1);
		aResultToken.symbol = SYM_STRING;
		aResultToken.marker = persistent;
		aResultToken.marker_length = (int)w.size();
		break;
	}
	default: // Ptr / unspecified: return as integer.
		aResultToken.value_int64 = (__int64)return_value.UIntPtr;
		break;
	}

	// Write back output parameters.
	for (int i = 0; i < arg_count; ++i)
	{
		DYNAPARM &p = dyna_param[i];
		ExprTokenType &val = *aParam[i * 2 + 1];
		if (!p.passed_by_address)
			continue;

		// Only VarRef (&Var) arguments are copied back; a plain variable is
		// input-only (var_usage == VARREF_READ).  Matches upstream:
		// "Output parameters are copied back only if provided with a VarRef."
		if (val.symbol != SYM_VAR || !val.var || !VARREF_IS_WRITE(val.var_usage))
			continue;
		{
			switch (p.type)
			{
			case DLL_ARG_INT:
				val.var->Assign(*(int *)p.ptr);
				break;
			case DLL_ARG_INT64:
				val.var->Assign(*(__int64 *)p.ptr);
				break;
			case DLL_ARG_SHORT:
				val.var->Assign((int)(short)(unsigned short)*(short *)p.ptr);
				break;
			case DLL_ARG_CHAR:
				val.var->Assign((int)(char)(unsigned char)*(char *)p.ptr);
				break;
			case DLL_ARG_FLOAT:
				val.var->Assign(*(float *)p.ptr);
				break;
			case DLL_ARG_DOUBLE:
				val.var->Assign(*(double *)p.ptr);
				break;
			case DLL_ARG_ASTR:
				if (p.ptr)
				{
					std::wstring w;
					const char *s = (const char *)p.ptr;
					if (s && *s)
					{
						size_t n = strlen(s);
						size_t k = 0;
						while (k < n)
						{
							unsigned char c = (unsigned char)s[k];
							unsigned int cp = 0;
							int extra = 0;
							if (c < 0x80) { cp = c; }
							else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
							else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
							else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
							else { ++k; continue; }
							if (k + extra >= n) break;
							bool okc = true;
							for (int j = 1; j <= extra; ++j)
							{
								unsigned char cc = (unsigned char)s[k + j];
								if ((cc & 0xC0) != 0x80) { okc = false; break; }
								cp = (cp << 6) | (cc & 0x3F);
							}
							if (!okc) { ++k; continue; }
							w += (wchar_t)cp;
							k += extra + 1;
						}
					}
					val.var->Assign(w.c_str());
				}
				break;
			default:
				break;
			}
		}
	}

	// Free per-arg by-address storage (p.is_hresult is reused as an
	// "owns buffer" flag: set only for buffers we allocated ourselves).
	for (int i = 0; i < arg_count; ++i)
	{
		DYNAPARM &p = dyna_param[i];
		if (p.passed_by_address && p.is_hresult)
		{
			free(p.ptr);
			p.ptr = nullptr;
		}
	}
	free(dyna_param);
	LinuxDllCallStrBufs().clear();
}

BIF_DECL(BIF_DllCall)
{
	LinuxDllCallImpl(aResultToken, aParam, aParamCount, _f_callee_id == FID_ComCall);
}
