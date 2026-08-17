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
};

static std::mutex s_callback_mutex;
static std::vector<LinuxCallback *> s_callbacks;

// The libffi trampoline: receives the raw argument pointer array.
static void LinuxCallbackInvoke(ffi_cif *aCif, void *aRet, void **aArgs, void *aUser)
{
	(void)aCif;
	LinuxCallback &cb = *(LinuxCallback *)aUser;

	__int64 params[256];
	for (int i = 0; i < cb.param_count && i < 256; ++i)
		params[i] = *(UINT_PTR *)aArgs[i];

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

	__int64 number_to_return = 0;
	FuncResult result_token;
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
			param[i].SetValue((UINT_PTR)params[i]);
	}

	CallMethod(cb.func, cb.func, nullptr, param, param_count, &number_to_return);

	if (cb.create_new_thread)
		ResumeUnderlyingThread();
	else if (pause_after_execute)
	{
		g->IsPaused = true;
		++g_nPausedThreads;
	}

	*(UINT_PTR *)aRet = (UINT_PTR)number_to_return;
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

	// Build the ffi call description: all arguments are pointers to
	// UINT_PTR-sized slots (the closure receives pointer-sized args).
	ffi_type **arg_types = (ffi_type **)calloc(actual_param_count ? (size_t)actual_param_count : 1, sizeof(ffi_type *));
	if (!arg_types)
	{
		delete cb;
		return FR_E_OUTOFMEM;
	}
	for (int i = 0; i < actual_param_count; ++i)
		arg_types[i] = &ffi_type_ulong;
	cb->arg_types = arg_types;

	if (ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, (unsigned)actual_param_count,
		&ffi_type_ulong, arg_types) != FFI_OK)
	{
		free(arg_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}

	cb->closure = (ffi_closure *)ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
	if (!cb->closure)
	{
		free(arg_types);
		delete cb;
		return FR_E_OUTOFMEM;
	}

	if (ffi_prep_closure_loc(cb->closure, &cb->cif, LinuxCallbackInvoke, cb, cb->code) != FFI_OK)
	{
		ffi_closure_free(cb->closure);
		free(arg_types);
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
			s_callbacks.erase(it);
			delete cb;
			return OK;
		}
	}
	return FR_E_ARG(0);
}
