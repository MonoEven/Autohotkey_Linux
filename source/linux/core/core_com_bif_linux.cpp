// D-Bus COM built-ins for the Linux port.  Replaces the Windows COM
// implementations from script_com.cpp with D-Bus equivalents; see
// core_com_dbus_linux.h for the mapping.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../script_object.h"
#include "core_com_dbus_linux.h"

// ---------------------------------------------------------------------------
// DefineComPrototypeMembers -- called from InitClasses()
// ---------------------------------------------------------------------------

void DefineComPrototypeMembers()
{
	Object::DefineMembers(Object::sComValuePrototype, _T("ComValue"), ComObject::sValueMembers, 1);
	Object::DefineMembers(Object::sComRefPrototype, _T("ComValueRef"), ComObject::sRefMembers, 1);
	Object::DefineMetadataMembers(Object::sComArrayPrototype, _T("ComObjArray"), ComObject::sArrayMembers, 6);
}

// ---------------------------------------------------------------------------
// ComValue.Call / ComObject.Call / ComObjFromPtr
// ---------------------------------------------------------------------------

// ComValue(type, value [, flags]) -- typed scalar wrapper.
// ComObject(serviceSpec [, unusedIID] [, unusedFlags]) -- D-Bus proxy.
// Callers (ComValue_Call / ComObject_Call / BIF_ComObj) have already
// excluded the `this` parameter where applicable, so aParam[0] is always
// the first real argument (mirrors upstream script_com.cpp).
static void LinuxComCallImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aIsComObject)
{
	if (aParamCount < 1)
		_f_throw_param(0);

	ExprTokenType &first = *aParam[0];

	if (aIsComObject)
	{
		// ComObject(spec [, iid]): spec is a service name / path / interface.
		LPTSTR spec = TokenToString(first);
		ComObject *obj = LinuxComNewProxy(spec);
		if (!obj)
			_f_throw_value(ERR_PARAM1_INVALID);
		if (obj->mConn)
			_f_return(obj);
		// No bus: still return the object (calls will fail with a clear error).
		_f_return(obj);
	}

	// ComValue(type, value [, flags])
	if (!TokenIsNumeric(first))
		_f_throw_param(0, _T("Number"));
	int vtype = (int)TokenToInt64(first);
	if (aParamCount < 2)
	{
		// ComObjFromPtr-like: type only -> proxy tag.
		ComObject *obj = LinuxComNewValue(0, vtype);
		_f_return(obj);
	}
	USHORT flags = 0;
	if (aParamCount > 2)
		flags = (USHORT)TokenToInt64(*aParam[2]);

	ExprTokenType &val = *aParam[1];
	if (vtype == 9 || vtype == 13) // VT_DISPATCH / VT_UNKNOWN: wrap a string spec as a proxy.
	{
		LPTSTR spec = TokenToString(val);
		ComObject *obj = LinuxComNewProxy(spec);
		if (obj)
			_f_return(obj);
		_f_throw_value(ERR_PARAM1_INVALID);
	}

	__int64 ival = 0;
	double dval = 0;
	bool isFloat = false;
	switch (vtype)
	{
	case 2: case 3: case 11: case 14: case 24: case 25:
		if (!TokenIsNumeric(val) && !TokenToObject(val))
			_f_throw_type(_T("Number"), val);
		ival = TokenToInt64(val);
		break;
	case 4: case 5:
		dval = TokenToDouble(val);
		isFloat = true;
		break;
	case 8: // BSTR -> store as a string in mVal64?  Keep pointer-free: store the
		// string's first characters is not feasible; instead keep the value as
		// the string itself via a small wrapper.  For D-Bus we mainly need the
		// type tag for argument conversion, so store the string in a side map
		// is overkill; just keep ival = 0 and rely on the caller's token for
		// the value (arguments pass the token directly).
		ival = 0;
		break;
	default:
		ival = TokenToInt64(val);
		break;
	}
	ComObject *obj = LinuxComNewValue(isFloat ? (__int64)(void *)&dval : ival, vtype, flags);
	if (isFloat)
	{
		// Store the double in the object via memcpy into mVal64.
		memcpy(&obj->mVal64, &dval, sizeof(dval));
	}
	_f_return(obj);
}

BIF_DECL(ComValue_Call)
{
	// Exclude the `this` class parameter (upstream script_com.cpp does the
	// same before passing control to BIF_ComObj).
	++aParam;
	--aParamCount;
	LinuxComCallImpl(aResultToken, aParam, aParamCount, false);
}

BIF_DECL(ComObject_Call)
{
	++aParam;
	--aParamCount;
	LinuxComCallImpl(aResultToken, aParam, aParamCount, true);
}

BIF_DECL(BIF_ComObj) // ComObjFromPtr -- no `this` parameter; aParam[0] is the pointer.
{
	// Linux has no COM interface pointers; keep the function callable (the
	// value is treated as an opaque handle) instead of crashing on the
	// missing `this` skip that class constructors perform.
	LinuxComCallImpl(aResultToken, aParam, aParamCount, false);
}

// ---------------------------------------------------------------------------
// ComObjGet / ComObjActive
// ---------------------------------------------------------------------------

BIF_DECL(BIF_ComObjGet)
{
	// ComObjGet(name): connect to a named service (D-Bus).
	LPTSTR spec = TokenToString(*aParam[0]);
	ComObject *obj = LinuxComNewProxy(spec);
	if (!obj)
	{
		aResultToken.SetValue(_T(""), 0);
		LinuxComError(aResultToken, "COM: invalid service specification", "");
		return;
	}
	_f_return(obj);
}

BIF_DECL(BIF_ComObjActive)
{
	// No "active object" concept on Linux; treat like ComObjGet.
	LPTSTR spec = TokenToString(*aParam[0]);
	ComObject *obj = LinuxComNewProxy(spec);
	if (!obj)
	{
		aResultToken.SetValue(_T(""), 0);
		LinuxComError(aResultToken, "COM: invalid service specification", "");
		return;
	}
	_f_return(obj);
}

// ---------------------------------------------------------------------------
// ComObjType / ComObjValue / ComObjFlags
// ---------------------------------------------------------------------------

BIF_DECL(BIF_ComObjType)
{
	ComObject *obj = dynamic_cast<ComObject *>(TokenToObject(*aParam[0]));
	if (!obj)
		_f_return_empty;
	if (aParamCount < 2)
	{
		aResultToken.value_int64 = obj->mVarType;
	}
	else
	{
		aResultToken.symbol = SYM_STRING;
		aResultToken.marker = _T("");
		aResultToken.marker_length = 0;
		LPTSTR requested_info = TokenToString(*aParam[1]);
		if (!_tcsicmp(requested_info, _T("Name")))
		{
			// Return a friendly type name.
			static const LPTSTR names[] = { _T("ComObject"), _T("ComValue"), _T("ComObjArray") };
			aResultToken.marker = obj->mIsProxy ? names[0] : names[1];
			aResultToken.marker_length = (int)_tcslen(aResultToken.marker);
		}
		else if (!_tcsicmp(requested_info, _T("Class")))
		{
			aResultToken.marker = obj->mIsProxy ? _T("ComObject") : _T("ComValue");
			aResultToken.marker_length = (int)_tcslen(aResultToken.marker);
		}
		else if (!_tcsicmp(requested_info, _T("IID")))
		{
			aResultToken.marker = obj->mIsProxy ? _T("(D-Bus)") : _T("(value)");
			aResultToken.marker_length = (int)_tcslen(aResultToken.marker);
		}
		else if (!_tcsicmp(requested_info, _T("CLSID")))
		{
			aResultToken.marker = obj->mService ? obj->mService : _T("");
			// mService is char*; convert.
			if (obj->mService)
			{
				std::wstring w;
				size_t n = strlen(obj->mService);
				w.resize(n);
				mbstowcs(&w[0], obj->mService, n);
				LPTSTR p = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
				tmemcpy(p, w.c_str(), n + 1);
				aResultToken.marker = p;
				aResultToken.marker_length = (int)n;
			}
		}
		// Anything else: empty string (like upstream for unknown info).
	}
}

BIF_DECL(BIF_ComObjValue)
{
	ComObject *obj = dynamic_cast<ComObject *>(TokenToObject(*aParam[0]));
	if (!obj)
		_f_throw_param(0, _T("ComValue"));
	if (obj->mVarType == 4 || obj->mVarType == 5) // Float stored in mVal64 bits.
	{
		double d;
		memcpy(&d, &obj->mVal64, sizeof(d));
		aResultToken.symbol = SYM_FLOAT;
		aResultToken.value_double = d;
		return;
	}
	aResultToken.value_int64 = obj->mVal64;
}

BIF_DECL(BIF_ComObjFlags)
{
	ComObject *obj = dynamic_cast<ComObject *>(TokenToObject(*aParam[0]));
	if (!obj)
		_f_throw_param(0, _T("ComValue"));
	if (aParamCount > 1)
	{
		USHORT flags, mask;
		if (aParamCount > 2)
		{
			flags = (USHORT)TokenToInt64(*aParam[1]);
			mask = (USHORT)TokenToInt64(*aParam[2]);
		}
		else
		{
			__int64 bigflags = TokenToInt64(*aParam[1]);
			if (bigflags < 0)
			{
				flags = 0;
				mask = (USHORT)-bigflags;
			}
			else
			{
				flags = (USHORT)bigflags;
				mask = flags;
			}
		}
		obj->mFlags = (obj->mFlags & ~mask) | (flags & mask);
	}
	aResultToken.value_int64 = obj->mFlags;
}

// ---------------------------------------------------------------------------
// ComObjQuery / ComObjConnect / ComObjArray (not applicable to D-Bus)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_ComObjQuery)
{
	LinuxComError(aResultToken, "COM: ComObjQuery is not supported on Linux (D-Bus has no interface pointers)");
}

BIF_DECL(BIF_ComObjConnect)
{
	LinuxComError(aResultToken, "COM: ComObjConnect is not supported on Linux (no COM events; use signal subscriptions via D-Bus tools instead)");
}

// ComObjArray(type, count...) -- not supported; raise a clear error.
BIF_DECL(ComObjArray_Call)
{
	LinuxComError(aResultToken, "COM: ComObjArray is not supported on Linux; use AutoHotkey Array instead");
}
