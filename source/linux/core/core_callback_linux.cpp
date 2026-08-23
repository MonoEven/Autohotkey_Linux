// Linux implementation of CallbackCreate/CallbackFree using libffi
// closures (the same library that powers DllCall).
//
// Windows builds a small assembly stub that jumps into
// RegisterCallbackCStub; on Linux an ffi_closure provides the same
// callable-address behaviour: the address returned by CallbackCreate can be
// passed to DllCall (or any C function expecting a callback pointer) and the
// closure trampoline marshals the arguments.
//
// Semantics follow upstream lib/CCallback.cpp:
//   - args are passed as 64-bit integers (UINT_PTR[] on Windows); with the
//     '&' option a single pointer to the argument array is passed instead.
//   - options 'F' selects "fast mode" (no new thread); by default a new
//     thread context is created for the call, like upstream.  Since Linux
//     callbacks are almost always invoked synchronously from the script
//     thread (via DllCall), we execute directly in the calling thread with a
//     fresh thread context (InitNewThread/ResumeUnderlyingThread), which
//     mirrors the default upstream behaviour.
//   - CallbackFree(addr) releases the closure and its resources.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../application.h"

#include <ffi.h>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------

// Callback ABI type tags (check_detail0821 §5-M4 / R4): the v2 options may
// name Float/Double so libffi places them in the SysV FP registers correctly
// (the old code used UINT_PTR slots for everything).
enum CallbackArgType { CB_PTR, CB_FLOAT, CB_DOUBLE };

struct LinuxCallback
{
	ffi_cif cif;
	ffi_closure *closure;
	void *code;
	IObject *func;              // Script function object (AddRef'd).
	int param_count;            // Number of 64-bit parameters.
	bool pass_params_pointer;   // '&' option.
	bool create_new_thread;     // Default true unless 'F'.
	ffi_type **arg_types;       // Owned; ffi_cif.arg_types points here.
	CallbackArgType *param_types; // Per-parameter ABI type (owned).
	CallbackArgType return_type;  // Return ABI type.
};

static std::mutex s_callback_mutex;
static std::vector<LinuxCallback *> s_callbacks;

// The libffi trampoline: receives the raw argument pointer array.
static void LinuxCallbackInvoke(ffi_cif *aCif, void *aRet, void **aArgs, void *aUser)
{
	(void)aCif;
	LinuxCallback &cb = *(LinuxCallback *)aUser;

	__int64 params[256];
	double fparams[256];
	for (int i = 0; i < cb.param_count && i < 256; ++i)
	{
		switch (cb.param_types ? cb.param_types[i] : CB_PTR)
		{
		case CB_FLOAT:  fparams[i] = *(float *)aArgs[i];  break;
		case CB_DOUBLE: fparams[i] = *(double *)aArgs[i]; break;
		default:        params[i] = *(UINT_PTR *)aArgs[i]; break;
		}
	}

	BOOL pause_after_execute = FALSE;
	if (cb.create_new_thread)
	{
		if (g_nThreads >= g_MaxThreadsTotal)
		{
			*(UINT_PTR *)aRet = 0;
			return;
		}
		InitNewThread(0, false, true);
	}
	else
	{
		if (g && g->IsPaused)
		{
			pause_after_execute = TRUE;
			g->IsPaused = false;
			--g_nPausedThreads;
		}
	}

	ExprTokenType *param = nullptr, one_param;
	int param_count;
	if (cb.pass_params_pointer)
	{
		param_count = 1;
		param = &one_param;
		one_param.SetValue((UINT_PTR)params);
	}
	else
	{
		param_count = cb.param_count;
		param = (ExprTokenType *)_alloca(param_count * sizeof(ExprTokenType));
		for (int i = 0; i < param_count; ++i)
		{
			if (cb.param_types && cb.param_types[i] != CB_PTR)
				param[i].SetValue(fparams[i]); // SYM_NUMBER with the double.
			else
				param[i].SetValue((UINT_PTR)params[i]);
		}
	}

	// Call the script function directly so a Float/Double return keeps its
	// full precision (CallMethod() fills an int64 and truncates doubles).
	ExprTokenType this_token(cb.func);
	double number_to_return = 0;
	{
		FuncResult result_token;
		ExprTokenType **pparam = (ExprTokenType **)_alloca((param_count ? (size_t)param_count : 1) * sizeof(ExprTokenType *));
		for (int i = 0; i < param_count; ++i)
			pparam[i] = param + i;
		ResultType call_rc = cb.func->Invoke(result_token, IT_CALL, nullptr, this_token, pparam, param_count);
		if (call_rc != EARLY_EXIT && call_rc != FAIL)
			number_to_return = TokenToDouble(result_token);
		result_token.Free();
	}

	if (cb.create_new_thread)
		ResumeUnderlyingThread();
	else if (pause_after_execute)
	{
		g->IsPaused = true;
		++g_nPausedThreads;
	}

	switch (cb.return_type)
	{
	case CB_FLOAT:  *(float *)aRet = (float)number_to_return; break;
	case CB_DOUBLE: *(double *)aRet = (double)number_to_return; break;
	default:        *(UINT_PTR *)aRet = (UINT_PTR)number_to_return; break;
	}
}

// ---------------------------------------------------------------------------

bif_impl FResult CallbackCreate(IObject *func, optl<StrArg> aOptions, optl<int> aParamCount, UINT_PTR &aRetVal)
{
	auto options = aOptions.value_or_empty();
	bool pass_params_pointer = _tcschr(options, '&') != nullptr;
	bool use_cdecl = StrChrAny(options, _T("Cc"));
	(void)use_cdecl; // No stack-cleanup difference on the SysV ABI.

	bool params_specified = aParamCount.has_value();
	int actual_param_count = aParamCount.value_or(0);
	if (params_specified ? (actual_param_count < 0 || actual_param_count > 255)
		: (pass_params_pointer && !use_cdecl))
		return FR_E_ARG(2);

	ResultToken result_token;
	auto fr = ValidateFunctor(func
		, pass_params_pointer ? 1 : actual_param_count
		, params_specified || pass_params_pointer ? nullptr : &actual_param_count);
	if (fr != OK)
		return fr;

	auto cb = new (std::nothrow) LinuxCallback();
	if (!cb)
		return FR_E_OUTOFMEM;
	memset(cb, 0, sizeof(*cb));
	cb->func = func;
	cb->param_count = actual_param_count;
	cb->pass_params_pointer = pass_params_pointer;
	cb->create_new_thread = !StrChrAny(options, _T("Ff"));
	// On Linux, callbacks are always invoked from the script thread (via
	// DllCall), so new-thread mode is unnecessary.  Default to fast mode
	// (no new thread) unless the user explicitly requests 'F'?  For now,
	// follow upstream default (create new thread when 'F' is absent).
	// If 'F' is specified, skip the new thread (fast mode).

	// Parse the ABI type words from the options (check_detail0821 §5-M4 / R4):
	// option words (CDecl/C/Fast/F/&) are skipped; the FIRST type name is the
	// return type, the following ones the parameter types
	// (Float/Double/Int/Int64/Short/Char/Ptr, case-insensitive).  This mirrors
	// the DllCall type-name convention.
	auto type_word = [](const TCHAR *&p) -> CallbackArgType {
		for (;;)
		{
			while (*p == _T(' ') || *p == _T('\t'))
				++p;
			if (!*p)
				return CB_PTR;
			const TCHAR *start = p;
			while (*p && *p != _T(' ') && *p != _T('\t'))
				++p;
			size_t n = (size_t)(p - start);
			if ((n == 1 && (*start == _T('C') || *start == _T('c') || *start == _T('F') || *start == _T('f')))
				|| (n == 5 && !_tcsnicmp(start, _T("CDecl"), 5))
				|| (n == 4 && !_tcsnicmp(start, _T("Fast"), 4)))
				continue; // Option word, not a type.
			if (n == 5 && !_tcsnicmp(start, _T("Float"), 5))
				return CB_FLOAT;
			if (n == 6 && !_tcsnicmp(start, _T("Double"), 6))
				return CB_DOUBLE;
			return CB_PTR; // Int/Int64/UInt/Short/Char/Ptr: pointer-sized slot.
		}
	};
	ffi_type **arg_types = (ffi_type **)calloc(actual_param_count ? (size_t)actual_param_count : 1, sizeof(ffi_type *));
	CallbackArgType *param_types = (CallbackArgType *)calloc(actual_param_count ? (size_t)actual_param_count : 1, sizeof(CallbackArgType));
	if (!arg_types || !param_types)
	{
		free(arg_types);
		free(param_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}
	const TCHAR *opt = options;
	cb->return_type = type_word(opt); // First type name = return type.
	for (int i = 0; i < actual_param_count; ++i)
	{
		CallbackArgType t = type_word(opt);
		param_types[i] = t;
		switch (t)
		{
		case CB_FLOAT:  arg_types[i] = &ffi_type_float;  break;
		case CB_DOUBLE: arg_types[i] = &ffi_type_double; break;
		default:        arg_types[i] = &ffi_type_ulong;  break;
		}
	}
	cb->arg_types = arg_types;
	cb->param_types = param_types;
	ffi_type *ret_type = &ffi_type_ulong;
	if (cb->return_type == CB_FLOAT)
		ret_type = &ffi_type_float;
	else if (cb->return_type == CB_DOUBLE)
		ret_type = &ffi_type_double;

	if (ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, (unsigned)actual_param_count,
		ret_type, arg_types) != FFI_OK)
	{
		free(arg_types);
		free(param_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}

	cb->closure = (ffi_closure *)ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
	if (!cb->closure)
	{
		free(arg_types);
		free(param_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}

	if (ffi_prep_closure_loc(cb->closure, &cb->cif, LinuxCallbackInvoke, cb, cb->code) != FFI_OK)
	{
		ffi_closure_free(cb->closure);
		free(arg_types);
		free(param_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}

	// NOTE: arg_types is NOT freed here — ffi_cif.arg_types references it,
	// and the cif is used each time the closure is invoked.  It is owned by
	// the callback and freed in CallbackFree.
	func->AddRef();

	{
		std::lock_guard<std::mutex> lock(s_callback_mutex);
		s_callbacks.push_back(cb);
	}

	aRetVal = (UINT_PTR)cb->code;
	return OK;
}

bif_impl FResult CallbackFree(UINT_PTR aCallback)
{
	if (aCallback < 65536)
		return FR_E_ARG(0);

	std::lock_guard<std::mutex> lock(s_callback_mutex);
	for (auto it = s_callbacks.begin(); it != s_callbacks.end(); ++it)
	{
		LinuxCallback *cb = *it;
		if ((UINT_PTR)cb->code == aCallback)
		{
			cb->func->Release();
			ffi_closure_free(cb->closure);
			if (cb->arg_types)
				free(cb->arg_types);
			if (cb->param_types)
				free(cb->param_types);
			s_callbacks.erase(it);
			delete cb;
			return OK;
		}
	}
	return FR_E_ARG(0);
}
