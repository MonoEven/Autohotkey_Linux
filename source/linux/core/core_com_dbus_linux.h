// Linux COM layer implemented over D-Bus (libdbus).
//
// Windows COM (IDispatch) does not exist on Linux; the D-Bus desktop bus
// is the native IPC equivalent.  This module maps the AutoHotkey COM API
// onto D-Bus:
//
//   ComObject("org.freedesktop.DBus")           -> proxy for a bus service
//   ComObject("svc/object/path")                -> proxy with a path
//   ComObject("svc/object/path/interface")      -> proxy with interface
//   obj.Method(args...)                         -> D-Bus method call
//   obj.Prop / obj.Prop := v                    -> Properties.Get / .Set
//   ComValue(type, value)                       -> typed scalar wrapper
//   ComObjType / ComObjValue / ComObjFlags      -> introspection helpers
//
// Interfaces that have no D-Bus equivalent (ComObjConnect, ComObjQuery,
// ComObjActive, SafeArray-specific ops) raise a clear error.
//
// The scalar "type" codes reuse the Windows VARTYPE numbers so scripts
// that use ComValue(3, 5) etc. keep working where meaningful:
//   2  VT_I2    3  VT_I4    4  VT_R4    5  VT_R8    8  VT_BSTR
//   11 VT_BOOL  14 VT_UI1 (byte)        24 VT_I8    25 VT_UI8
//   9  VT_DISPATCH (proxy)  13 VT_UNKNOWN (proxy)

#pragma once

#include "../../stdafx.h"
#include "../../script.h"
#include "../../script_object.h"
#include <dbus/dbus.h>

// Linux COM wrapper object (replaces the Windows ComObject from
// script_com.h, which is not compiled on Linux).
class ComObject : public ObjectBase
{
public:
	enum
	{
		P_Ptr,
		// ComValueRef
		P___Item,
	};

	__int64 mVal64 = 0;   // Scalar value, or 0 for a proxy.
	int mVarType = 0;     // VARTYPE-style tag (see above).
	USHORT mFlags = 0;

	// D-Bus proxy state (valid when mVarType is VT_DISPATCH/VT_UNKNOWN).
	DBusConnection *mConn = nullptr; // Borrowed; not freed by us.
	char *mService = nullptr;
	char *mPath = nullptr;
	char *mIface = nullptr; // May be nullptr: use introspection fallback.
	bool mIsProxy = false;

	ComObject() {}
	~ComObject()
	{
		free(mService);
		free(mPath);
		free(mIface);
	}

	// IObject: script obj.Member(...) dispatch (D-Bus method calls).
	ResultType Invoke(IObject_Invoke_PARAMS_DECL) override;
	// ID-based members (Ptr / __Item).
	void Invoke(ResultToken &aResultToken, int aID, int aFlags, ExprTokenType *aParam[], int aParamCount);
	IObject_Type_Impl("ComObject")
	Object *Base()
	{
		// Proxies expose the ComObject prototype; scalar wrappers expose
		// the ComValue prototype (which provides the Ptr property).
		return mIsProxy ? Object::sComObjectPrototype : Object::sComValuePrototype;
	}
	bool IsOfType(Object *aPrototype);
	IObject_DebugWriteProperty_Def

	// SafeArray-ish members (limited support on Linux).
	FResult SafeArray_Item(VariantParams &aParam, ExprTokenType *aNewValue, ResultToken *aResultToken);
	FResult set_SafeArray_Item(ExprTokenType &aNewValue, VariantParams &aParam) { return SafeArray_Item(aParam, &aNewValue, nullptr); }
	FResult get_SafeArray_Item(ResultToken &aResultToken, VariantParams &aParam) { return SafeArray_Item(aParam, nullptr, &aResultToken); }
	FResult SafeArray_Enum(optl<int>, IObject *&aRetVal);
	FResult SafeArray_Clone(IObject *&aRetVal);
	FResult SafeArray_MaxIndex(optl<UINT> aDims, int &aRetVal);
	FResult SafeArray_MinIndex(optl<UINT> aDims, int &aRetVal);

	static ObjectMember sRefMembers[], sValueMembers[];
	static ObjectMemberMd sArrayMembers[];
};

// Helpers shared with the BIF implementations.
void LinuxComError(ResultToken &aResultToken, const char *aWhat, const char *aDetail = nullptr);
ComObject *LinuxComNewValue(__int64 aVal, int aVarType, USHORT aFlags = 0);
ComObject *LinuxComNewProxy(const wchar_t *aSpec);
