// Linux COM layer implemented over D-Bus (libdbus).  See the header for
// the mapping between the AutoHotkey COM API and the desktop bus.
#include "core_com_dbus_linux.h"

#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../StringConv.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void LinuxComError(ResultToken &aResultToken, const char *aWhat, const char *aDetail)
{
	// OSError with a descriptive message.
	char buf[1024];
	if (aDetail && *aDetail)
		snprintf(buf, sizeof(buf), "%s: %s", aWhat, aDetail);
	else
		snprintf(buf, sizeof(buf), "%s", aWhat);
	wchar_t wbuf[1024];
	mbstowcs(wbuf, buf, 1023);
	wbuf[1023] = 0;
	aResultToken.Error(wbuf, ErrorPrototype::OS);
}

ComObject *LinuxComNewValue(__int64 aVal, int aVarType, USHORT aFlags)
{
	ComObject *obj = new ComObject();
	obj->mVal64 = aVal;
	obj->mVarType = aVarType;
	obj->mFlags = aFlags;
	return obj;
}

#ifdef CONFIG_DEBUGGER
void ComObject::DebugWriteProperty(IDebugProperties *aDebugger, int aPage, int aPageSize, int aMaxDepth)
{
	constexpr int kChildCount = 7;
	DebugCookie cookie;
	aDebugger->BeginProperty(nullptr, "object", kChildCount, cookie);
	if (aMaxDepth > 0)
	{
		int first = max(0, aPage * aPageSize);
		int last = min(kChildCount, first + aPageSize);
		for (int index = first; index < last; ++index)
		{
			ExprTokenType value;
			CStringTCharFromUTF8 text("");
			switch (index)
			{
			case 0: value.SetValue((__int64)mVarType); aDebugger->WriteProperty("VarType", value); break;
			case 1: value.SetValue((__int64)mFlags); aDebugger->WriteProperty("Flags", value); break;
			case 2: value.SetValue((__int64)(mIsProxy ? 1 : 0)); aDebugger->WriteProperty("IsProxy", value); break;
			case 3:
				text = CStringTCharFromUTF8(mService ? mService : "");
				value.SetValue((LPTSTR)text.GetString(), text.GetLength());
				aDebugger->WriteProperty("Service", value);
				break;
			case 4:
				text = CStringTCharFromUTF8(mPath ? mPath : "");
				value.SetValue((LPTSTR)text.GetString(), text.GetLength());
				aDebugger->WriteProperty("Path", value);
				break;
			case 5:
				text = CStringTCharFromUTF8(mIface ? mIface : "");
				value.SetValue((LPTSTR)text.GetString(), text.GetLength());
				aDebugger->WriteProperty("Interface", value);
				break;
			case 6: value.SetValue(mVal64); aDebugger->WriteProperty("Value", value); break;
			}
		}
	}
	aDebugger->EndProperty(cookie);
}
#endif

// Parse a proxy spec: "service", "service/path", "service/path/interface".
// The service part is the first component before any '/'; everything after
// is split into path (may contain further '/') and an optional final
// interface (only when the path has 2+ components after the service? No:
// we can't reliably distinguish path from interface, so we require the
// interface to be given explicitly when it differs from the service).
// Convention used here (documented in linux-port.htm):
//   "svc"                     -> service, path "/", interface = service
//   "svc/path"                -> service, path, interface = service
//   "svc/path/iface"          -> service, path, interface
ComObject *LinuxComNewProxy(const wchar_t *aSpec)
{
	char spec[1024];
	if (wcstombs(spec, aSpec, sizeof(spec) - 1) == (size_t)-1)
		return nullptr;
	spec[sizeof(spec) - 1] = '\0';

	char service[256], path[512], iface[256];
	service[0] = path[0] = iface[0] = '\0';

	const char *slash = strchr(spec, '/');
	if (!slash)
	{
		strncpy(service, spec, sizeof(service) - 1);
		strncpy(iface, spec, sizeof(iface) - 1);
		strcpy(path, "/");
	}
	else
	{
		size_t svclen = (size_t)(slash - spec);
		if (svclen >= sizeof(service))
			svclen = sizeof(service) - 1;
		memcpy(service, spec, svclen);
		service[svclen] = '\0';
		const char *rest = slash; // points at '/'
		// Count '/' in rest: path may contain slashes (object paths do).
		const char *last = strrchr(rest, '/');
		// If there are at least two '/' in the whole spec after the service
		// separator, treat the part after the last '/' as the interface.
		// e.g. "svc/a/b" -> path "/a", iface "b".
		//      "svc/a"   -> path "/a", iface = service.
		const char *p;
		int slashes = 0;
		for (p = rest; *p; ++p)
			if (*p == '/')
				++slashes;
		if (slashes >= 2)
		{
			// last points at the final '/'.
			size_t pathlen = (size_t)(last - rest);
			if (pathlen >= sizeof(path))
				pathlen = sizeof(path) - 1;
			memcpy(path, rest, pathlen);
			path[pathlen] = '\0';
			if (path[0] == '\0')
				strcpy(path, "/");
			strncpy(iface, last + 1, sizeof(iface) - 1);
		}
		else
		{
			strncpy(path, rest, sizeof(path) - 1);
			strncpy(iface, service, sizeof(iface) - 1);
		}
	}
	// Ensure D-Bus name validity for the service (starts with a letter/digit
	// and contains no '/'): if it looks like a bus address or invalid name,
	// still try (dbus will reject it and we surface the error).
	ComObject *obj = new ComObject();
	obj->mVarType = 9; // VT_DISPATCH
	obj->mIsProxy = true;
	obj->mService = strdup(service);
	obj->mPath = strdup(path);
	obj->mIface = strdup(iface);
	// Get the session bus (borrowed connection).
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!conn)
	{
		if (dbus_error_is_set(&err))
		{
			// Fall back to system bus.
			dbus_error_free(&err);
			dbus_error_init(&err);
			conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
		}
	}
	if (conn)
	{
		obj->mConn = conn;
	}
	return obj;
}

// ---------------------------------------------------------------------------
// Scalar <-> D-Bus conversion
// ---------------------------------------------------------------------------

// Append a script token to a D-Bus message iterator as the given AHK
// "type code" (VARTYPE number).  Returns false on unsupported type.
static bool LinuxComAppendArg(DBusMessageIter *aIter, int aType, ExprTokenType &aToken)
{
	switch (aType)
	{
	case 2: { // VT_I2
		dbus_int16_t v = (dbus_int16_t)TokenToInt64(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_INT16, &v);
	}
	case 3: { // VT_I4
		dbus_int32_t v = (dbus_int32_t)TokenToInt64(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_INT32, &v);
	}
	case 4: { // VT_R4
		double d = TokenToDouble(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_DOUBLE, &d);
	}
	case 5: { // VT_R8
		double d = TokenToDouble(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_DOUBLE, &d);
	}
	case 8: { // VT_BSTR
		TCHAR buf[2]; buf[0] = 0;
		size_t len = 0;
		LPTSTR str = TokenToString(aToken, buf, &len);
		if (!str)
			str = buf;
		char mb[4096];
		size_t n = wcstombs(mb, str, sizeof(mb) - 1);
		if (n == (size_t)-1)
			return false;
		mb[n] = '\0';
		const char *s = mb;
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_STRING, &s);
	}
	case 11: { // VT_BOOL
		dbus_bool_t v = TokenToInt64(aToken) != 0;
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_BOOLEAN, &v);
	}
	case 14: { // VT_UI1 (byte)
		unsigned char v = (unsigned char)TokenToInt64(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_BYTE, &v);
	}
	case 24: { // VT_I8
		dbus_int64_t v = TokenToInt64(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_INT64, &v);
	}
	case 25: { // VT_UI8
		dbus_uint64_t v = (dbus_uint64_t)TokenToInt64(aToken);
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_UINT64, &v);
	}
	case 9:  // VT_DISPATCH: pass a nested ComObject's spec string
	case 13: { // VT_UNKNOWN
		IObject *obj = TokenToObject(aToken);
		if (auto *co = dynamic_cast<ComObject *>(obj))
		{
			if (co->mIsProxy && co->mService)
			{
				const char *s = co->mService;
				return dbus_message_iter_append_basic(aIter, DBUS_TYPE_STRING, &s);
			}
		}
		// Plain string fallback.
		TCHAR buf[2]; buf[0] = 0;
		size_t len = 0;
		LPTSTR str = TokenToString(aToken, buf, &len);
		if (!str)
			str = buf;
		char mb[4096];
		size_t n = wcstombs(mb, str, sizeof(mb) - 1);
		if (n == (size_t)-1)
			return false;
		mb[n] = '\0';
		const char *s = mb;
		return dbus_message_iter_append_basic(aIter, DBUS_TYPE_STRING, &s);
	}
	default:
		return false;
	}
}

// Convert a D-Bus basic value (at aIter) to a ResultToken.
static void LinuxComReplyToToken(DBusMessageIter *aIter, ResultToken &aResultToken)
{
	int type = dbus_message_iter_get_arg_type(aIter);
	switch (type)
	{
	case DBUS_TYPE_BOOLEAN:
	{
		dbus_bool_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v ? 1 : 0;
		break;
	}
	case DBUS_TYPE_BYTE:
	{
		unsigned char v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_INT16:
	{
		dbus_int16_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_UINT16:
	{
		dbus_uint16_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_INT32:
	{
		dbus_int32_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_UINT32:
	{
		dbus_uint32_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_INT64:
	{
		dbus_int64_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = v;
		break;
	}
	case DBUS_TYPE_UINT64:
	{
		dbus_uint64_t v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = (__int64)v;
		break;
	}
	case DBUS_TYPE_DOUBLE:
	{
		double v;
		dbus_message_iter_get_basic(aIter, &v);
		aResultToken.symbol = SYM_FLOAT;
		aResultToken.value_double = v;
		break;
	}
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_SIGNATURE:
	{
		const char *s;
		dbus_message_iter_get_basic(aIter, &s);
		if (!s)
			s = "";
		size_t n = strlen(s);
		std::wstring w(n, L'\0');
		mbstowcs(&w[0], s, n);
		LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
		tmemcpy(persistent, w.c_str(), n + 1);
		aResultToken.symbol = SYM_STRING;
		aResultToken.marker = persistent;
		aResultToken.marker_length = (int)n;
		break;
	}
	case DBUS_TYPE_ARRAY:
	{
		// Return an Array of values (strings, numbers, ...).
		DBusMessageIter sub;
		dbus_message_iter_recurse(aIter, &sub);
		Array *arr = Array::Create();
		int elem = dbus_message_iter_get_arg_type(&sub);
		while (elem != DBUS_TYPE_INVALID)
		{
			ExprTokenType tok;
			tok.symbol = SYM_INTEGER;
			tok.value_int64 = 0;
			if (elem == DBUS_TYPE_BYTE)
			{
				unsigned char b;
				dbus_message_iter_get_basic(&sub, &b);
				tok.value_int64 = b;
			}
			else if (elem == DBUS_TYPE_BOOLEAN)
			{
				dbus_bool_t v;
				dbus_message_iter_get_basic(&sub, &v);
				tok.value_int64 = v ? 1 : 0;
			}
			else if (elem == DBUS_TYPE_INT16 || elem == DBUS_TYPE_UINT16
				|| elem == DBUS_TYPE_INT32 || elem == DBUS_TYPE_UINT32
				|| elem == DBUS_TYPE_INT64 || elem == DBUS_TYPE_UINT64)
			{
				dbus_int64_t v = 0;
				dbus_message_iter_get_basic(&sub, &v);
				tok.value_int64 = v;
			}
			else if (elem == DBUS_TYPE_DOUBLE)
			{
				double d;
				dbus_message_iter_get_basic(&sub, &d);
				tok.symbol = SYM_FLOAT;
				tok.value_double = d;
			}
			else if (elem == DBUS_TYPE_STRING || elem == DBUS_TYPE_OBJECT_PATH)
			{
				const char *s;
				dbus_message_iter_get_basic(&sub, &s);
				if (!s)
					s = "";
				size_t n = strlen(s);
				std::wstring w(n, L'\0');
				mbstowcs(&w[0], s, n);
				LPTSTR p = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
				tmemcpy(p, w.c_str(), n + 1);
				tok.symbol = SYM_STRING;
				tok.marker = p;
				tok.marker_length = (int)n;
			}
			// Append to the Array.
			arr->InsertAt(arr->Length(), &tok, 1);
			dbus_message_iter_next(&sub);
			elem = dbus_message_iter_get_arg_type(&sub);
		}
		aResultToken.SetValue(arr);
		break;
	}
	default:
		aResultToken.symbol = SYM_INTEGER;
		aResultToken.value_int64 = 0;
		break;
	}
}

// ---------------------------------------------------------------------------
// D-Bus method call / property access
// ---------------------------------------------------------------------------

// Perform a D-Bus call.  aMember is the method/property name, aIface the
// interface (may be null -> the proxy's own interface), aProperty indicates
// a property access (uses org.freedesktop.DBus.Properties).
static ResultType LinuxComDbusCall(ComObject &aObj, LPTSTR aMember, ExprTokenType *aParam[]
	, int aParamCount, bool aIsPropertyGet, bool aIsPropertySet, ResultToken &aResultToken)
{
	if (!aObj.mConn)
	{
		LinuxComError(aResultToken, "COM: no D-Bus connection available (is DBUS_SESSION_BUS_ADDRESS set?)");
		return aResultToken.Result();
	}
	if (!aObj.mService || !aObj.mPath)
	{
		LinuxComError(aResultToken, "COM: object is not a D-Bus proxy");
		return aResultToken.Result();
	}

	char member[512];
	if (wcstombs(member, aMember, sizeof(member) - 1) == (size_t)-1)
	{
		LinuxComError(aResultToken, "COM: invalid member name");
		return aResultToken.Result();
	}
	member[sizeof(member) - 1] = '\0';

	DBusMessage *msg = nullptr;
	const char *iface = aObj.mIface ? aObj.mIface : aObj.mService;
	if (aIsPropertyGet || aIsPropertySet)
	{
		msg = dbus_message_new_method_call(aObj.mService, aObj.mPath
			, "org.freedesktop.DBus.Properties", aIsPropertyGet ? "Get" : "Set");
		if (!msg)
		{
			LinuxComError(aResultToken, "COM: out of memory");
			return aResultToken.Result();
		}
		DBusMessageIter iter;
		dbus_message_iter_init_append(msg, &iter);
		const char *ifc = iface;
		const char *prop = member;
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifc);
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
		if (aIsPropertySet)
		{
			// Need a variant containing the value.
			DBusMessageIter variant;
			dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, nullptr, &variant);
			// Determine type from the argument's ComValue wrapper if any,
			// else default to string.
			int vtype = 8; // VT_BSTR
			IObject *vobj = TokenToObject(*aParam[0]);
			if (auto *co = dynamic_cast<ComObject *>(vobj))
				vtype = co->mVarType;
			if (!LinuxComAppendArg(&variant, vtype, *aParam[0]))
			{
				dbus_message_iter_close_container(&iter, &variant);
				dbus_message_unref(msg);
				LinuxComError(aResultToken, "COM: unsupported property value type");
				return aResultToken.Result();
			}
			dbus_message_iter_close_container(&iter, &variant);
		}
	}
	else
	{
		msg = dbus_message_new_method_call(aObj.mService, aObj.mPath, iface, member);
		if (!msg)
		{
			LinuxComError(aResultToken, "COM: out of memory");
			return aResultToken.Result();
		}
		DBusMessageIter iter;
		dbus_message_iter_init_append(msg, &iter);
		for (int i = 0; i < aParamCount; ++i)
		{
			// Type: prefer a ComValue wrapper's type code.
			int vtype = 8; // VT_BSTR
			IObject *vobj = TokenToObject(*aParam[i]);
			if (auto *co = dynamic_cast<ComObject *>(vobj))
				vtype = co->mVarType;
			if (!LinuxComAppendArg(&iter, vtype, *aParam[i]))
			{
				dbus_message_unref(msg);
				LinuxComError(aResultToken, "COM: unsupported argument type");
				return aResultToken.Result();
			}
		}
	}

	DBusError err;
	dbus_error_init(&err);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(aObj.mConn, msg, -1, &err);
	dbus_message_unref(msg);
	if (!reply)
	{
		if (dbus_error_is_set(&err))
			LinuxComError(aResultToken, "COM: D-Bus call failed", err.message);
		else
			LinuxComError(aResultToken, "COM: D-Bus call failed (no reply)");
		dbus_error_free(&err);
		return aResultToken.Result();
	}
	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
	{
		const char *ename = dbus_message_get_error_name(reply);
		LinuxComError(aResultToken, ename ? ename : "COM: D-Bus error");
		dbus_message_unref(reply);
		return aResultToken.Result();
	}

	if (!aIsPropertySet)
	{
		DBusMessageIter iter;
		dbus_message_iter_init(reply, &iter);
		if (aIsPropertyGet)
		{
			// Reply is a variant.
			DBusMessageIter variant;
			if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&iter, &variant);
				LinuxComReplyToToken(&variant, aResultToken);
			}
			else
				LinuxComReplyToToken(&iter, aResultToken);
		}
		else
		{
			// First reply value.
			LinuxComReplyToToken(&iter, aResultToken);
		}
	}
	dbus_message_unref(reply);
	return aResultToken.Result();

}

// ---------------------------------------------------------------------------
// ComObject::Invoke -- script obj.Method(args) / obj.Prop / obj.Prop := v
// ---------------------------------------------------------------------------

// ID-based members: Ptr (ComValue) and __Item (ComValueRef).
void ComObject::Invoke(ResultToken &aResultToken, int aID, int aFlags, ExprTokenType *aParam[], int aParamCount)
{
	if (aID == P___Item)
	{
		// ComValueRef __Item: read/write the underlying scalar.
		if (aFlags & IT_SET)
		{
			if (aParamCount < 1)
			{
				aResultToken.Error(_T("ComValueRef requires a value"));
				return;
			}
			if (mVarType == 4 || mVarType == 5)
			{
				double d = TokenToDouble(*aParam[0]);
				memcpy(&mVal64, &d, sizeof(d));
			}
			else
				mVal64 = TokenToInt64(*aParam[0]);
		}
		else
		{
			if (mVarType == 4 || mVarType == 5)
			{
				double d;
				memcpy(&d, &mVal64, sizeof(d));
				aResultToken.symbol = SYM_FLOAT;
				aResultToken.value_double = d;
			}
			else
			{
				aResultToken.symbol = SYM_INTEGER;
				aResultToken.value_int64 = mVal64;
			}
		}
	}
	else // P_Ptr
	{
		if (aFlags & IT_SET)
		{
			if (aParamCount < 1 || !ParamIndexIsNumeric(0))
			{
				aResultToken.Error(_T("Ptr requires a numeric value"));
				return;
			}
			mVal64 = ParamIndexToInt64(0);
		}
		else
		{
			aResultToken.symbol = SYM_INTEGER;
			aResultToken.value_int64 = mVal64;
		}
	}
}

ResultType ComObject::Invoke(IObject_Invoke_PARAMS_DECL)
{
	if (!mIsProxy || !mConn)
	{
		// Scalar wrapper: handle the Ptr property and (for ComValueRef)
		// __Item directly; anything else is unknown.
		if (!aName)
			return INVOKE_NOT_HANDLED;
		if (!_tcsicmp(aName, _T("Ptr")))
		{
			if (aFlags & IT_SET)
			{
				if (aParamCount < 1 || !ParamIndexIsNumeric(0))
					return aResultToken.TypeError(_T("Number"), *aParam[0]);
				mVal64 = ParamIndexToInt64(0);
				return OK;
			}
			aResultToken.symbol = SYM_INTEGER;
			aResultToken.value_int64 = mVal64;
			return OK;
		}
		if (!_tcsicmp(aName, _T("__Item")))
		{
			if (aFlags & IT_SET)
			{
				if (aParamCount < 1)
					return INVOKE_NOT_HANDLED;
				if (mVarType == 4 || mVarType == 5)
				{
					double d = TokenToDouble(*aParam[0]);
					memcpy(&mVal64, &d, sizeof(d));
				}
				else
					mVal64 = TokenToInt64(*aParam[0]);
				return OK;
			}
			if (mVarType == 4 || mVarType == 5)
			{
				double d;
				memcpy(&d, &mVal64, sizeof(d));
				aResultToken.symbol = SYM_FLOAT;
				aResultToken.value_double = d;
			}
			else
				aResultToken.value_int64 = mVal64;
			return OK;
		}
		return INVOKE_NOT_HANDLED;
	}

	// Property access vs method call:
	//   IT_GET with no parens = property get; IT_SET = property set;
	//   IT_CALL = method call.
	bool isPropGet = (aFlags & IT_GET) && !(aFlags & IT_CALL);
	bool isPropSet = !!(aFlags & IT_SET);
	bool isCall = !!(aFlags & IT_CALL);

	if (isPropSet)
	{
		if (aParamCount < 1)
			return INVOKE_NOT_HANDLED;
		// aParam[0] holds the value being assigned.
		ResultType r = LinuxComDbusCall(*this, aName, aParam + 0, 1, false, true, aResultToken);
		if (aResultToken.Exited())
			return r;
		aResultToken.SetValue(_T(""), 0); // Value ignored for SET.
		return OK;
	}
	if (isPropGet)
	{
		return LinuxComDbusCall(*this, aName, nullptr, 0, true, false, aResultToken);
	}
	// Method call.
	return LinuxComDbusCall(*this, aName, aParam, aParamCount, false, false, aResultToken);
}

bool ComObject::IsOfType(Object *aPrototype)
{
	return aPrototype == Object::sComObjectPrototype || aPrototype == Object::sComValuePrototype;
}

// ---------------------------------------------------------------------------
// SafeArray-ish members (limited: byte-array via D-Bus 'ay' not supported
// end-to-end; these raise a clear error for now).
// ---------------------------------------------------------------------------

FResult ComObject::SafeArray_Item(VariantParams &, ExprTokenType *, ResultToken *)
{
	return FError(_T("ComObjArray indexing is not supported on Linux; use an Array instead."));
}

FResult ComObject::SafeArray_Enum(optl<int>, IObject *&aRetVal)
{
	aRetVal = nullptr;
	return FError(_T("ComObjArray enumeration is not supported on Linux; use an Array instead."));
}

FResult ComObject::SafeArray_Clone(IObject *&aRetVal)
{
	aRetVal = nullptr;
	return FError(_T("ComObjArray.Clone is not supported on Linux."));
}

FResult ComObject::SafeArray_MaxIndex(optl<UINT>, int &aRetVal)
{
	aRetVal = 0;
	return FError(_T("ComObjArray.MaxIndex is not supported on Linux."));
}

FResult ComObject::SafeArray_MinIndex(optl<UINT>, int &aRetVal)
{
	aRetVal = 0;
	return FError(_T("ComObjArray.MinIndex is not supported on Linux."));
}

// ---------------------------------------------------------------------------
// Member tables
// ---------------------------------------------------------------------------

ObjectMember ComObject::sValueMembers[]
{
	Object_Property_get_set(Ptr),
};

ObjectMember ComObject::sRefMembers[]
{
	Object_Property_get_set(__Item),
};

ObjectMemberMd ComObject::sArrayMembers[]
{
	md_member_x(ComObject, __Item, SafeArray_Item, GET, (Ret, Variant, RetVal), (In, Params, Index)),
	md_member_x(ComObject, __Item, SafeArray_Item, SET, (In, Variant, Value), (In, Params, Index)),
	md_member_x(ComObject, __Enum, SafeArray_Enum, CALL, (In_Opt, Int32, N), (Ret, Object, RetVal)),
	md_member_x(ComObject, Clone, SafeArray_Clone, CALL, (Ret, Object, RetVal)),
	md_member_x(ComObject, MaxIndex, SafeArray_MaxIndex, CALL, (In_Opt, UInt32, Dims), (Ret, Int32, RetVal)),
	md_member_x(ComObject, MinIndex, SafeArray_MinIndex, CALL, (In_Opt, UInt32, Dims), (Ret, Int32, RetVal))
};
