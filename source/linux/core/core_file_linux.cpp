// Linux registration of the File class members.
//
// On Windows these are registered by Object::DefineMetadataMembers() using the
// DynaCall x64 assembly marshaler (MdFunc.cpp).  On Linux there is no DynaCall,
// so each FileObject member gets a small BIF-convention wrapper instead.
//
// FileObject's members are private; access is granted to the single friend
// function DefineFileClassLinux() (see TextIO.cpp), which fills a table of
// member-function pointers that the wrappers use.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_file_linux.h"

// Table of FileObject member function pointers, filled by DefineFileClassLinux()
// (TextIO.cpp) with Object::* member pointers.  FileObject derives from Object,
// so the wrappers can invoke the members through an Object*.

namespace
{

Object *LinuxThisFile(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount)
{
	if (aParamCount < 1)
	{
		aResultToken.Error(_T("Missing 'this'."));
		return nullptr;
	}
	Object *obj = TokenToObject(*aParam[0]);
	if (!obj || !LinuxIsFileObject(obj))
	{
		aResultToken.Error(_T("Invalid 'this' for File method."));
		return nullptr;
	}
	return obj;
}

void LinuxFileCheck(ResultToken &aResultToken, FResult aFr, ExprTokenType *aParam[], int aParamCount)
{
	if (FAILED(aFr))
		FResultToError(aResultToken, aParam, aParamCount, aFr, 0);
}

void LinuxFileStrRet(ResultToken &aResultToken, StrRet &aRet)
{
	if (aRet.Value())
	{
		if (aRet.UsedMalloc())
			aResultToken.AcceptMem(const_cast<LPTSTR>(aRet.Value()), aRet.Length());
		else
			aResultToken.SetValue(const_cast<LPTSTR>(aRet.Value()), aRet.Length());
	}
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(LinuxFile_Close)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
		LinuxFileCheck(aResultToken, (fo->*g_file_members.Close)(), aParam, aParamCount);
}

BIF_DECL(LinuxFile_Read)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT char_slot = 0;
		optl<UINT> chars = (aParamCount > 1 && !ParamIndexIsOmitted(1))
			? optl<UINT>(char_slot = (UINT)TokenToInt64(*aParam[1]))
			: optl<UINT>(nullptr);
		StrRet ret(aResultToken.buf);
		FResult fr = (fo->*g_file_members.Read)(chars, ret);
		if (FAILED(fr))
		{
			LinuxFileCheck(aResultToken, fr, aParam, aParamCount);
			return;
		}
		LinuxFileStrRet(aResultToken, ret);
	}
}

BIF_DECL(LinuxFile_Write)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.Write)(*aParam[1], ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_ReadLine)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		StrRet ret(aResultToken.buf);
		FResult fr = (fo->*g_file_members.ReadLine)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		LinuxFileStrRet(aResultToken, ret);
	}
}

BIF_DECL(LinuxFile_WriteLine)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.WriteLine)(aParamCount > 1 ? aParam[1] : nullptr, ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_Seek)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		__int64 distance = aParamCount > 1 ? TokenToInt64(*aParam[1]) : 0;
		int origin_slot = 0;
		optl<int> origin = (aParamCount > 2 && !ParamIndexIsOmitted(2))
			? optl<int>(origin_slot = (int)TokenToInt64(*aParam[2]))
			: optl<int>(nullptr);
		BOOL ret = FALSE;
		FResult fr = (fo->*g_file_members.Seek)(distance, origin, ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue(ret ? 1 : 0);
	}
}

BIF_DECL(LinuxFile_RawRead)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT bytes_slot = 0;
		optl<UINT> bytes = (aParamCount > 2 && !ParamIndexIsOmitted(2))
			? optl<UINT>(bytes_slot = (UINT)TokenToInt64(*aParam[2]))
			: optl<UINT>(nullptr);
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.RawRead)(*aParam[1], bytes, ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_RawWrite)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT bytes_slot = 0;
		optl<UINT> bytes = (aParamCount > 2 && !ParamIndexIsOmitted(2))
			? optl<UINT>(bytes_slot = (UINT)TokenToInt64(*aParam[2]))
			: optl<UINT>(nullptr);
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.RawWrite)(*aParam[1], bytes, ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

#define LINUX_FILE_READNUM(wrapname, type, member) \
BIF_DECL(wrapname) \
{ \
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount)) \
	{ \
		type ret = 0; \
		FResult fr = (fo->*g_file_members.member)(ret); \
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; } \
		aResultToken.SetValue((__int64)ret); \
	} \
}

#define LINUX_FILE_WRITENUM(wrapname, type, member) \
BIF_DECL(wrapname) \
{ \
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount)) \
	{ \
		UINT ret = 0; \
		FResult fr = (fo->*g_file_members.member)((type)TokenToInt64(*aParam[1]), ret); \
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; } \
		aResultToken.SetValue((__int64)ret); \
	} \
}

LINUX_FILE_READNUM(LinuxFile_ReadChar, INT8, ReadChar)
LINUX_FILE_READNUM(LinuxFile_ReadInt, int, ReadInt)
LINUX_FILE_READNUM(LinuxFile_ReadInt64, __int64, ReadInt64)
LINUX_FILE_READNUM(LinuxFile_ReadShort, INT16, ReadShort)
LINUX_FILE_READNUM(LinuxFile_ReadUChar, UINT8, ReadUChar)
LINUX_FILE_READNUM(LinuxFile_ReadUInt, UINT, ReadUInt)
LINUX_FILE_READNUM(LinuxFile_ReadUShort, UINT16, ReadUShort)

LINUX_FILE_WRITENUM(LinuxFile_WriteChar, INT8, WriteChar)
LINUX_FILE_WRITENUM(LinuxFile_WriteInt, int, WriteInt)
LINUX_FILE_WRITENUM(LinuxFile_WriteInt64, __int64, WriteInt64)
LINUX_FILE_WRITENUM(LinuxFile_WriteShort, INT16, WriteShort)
LINUX_FILE_WRITENUM(LinuxFile_WriteUChar, UINT8, WriteUChar)
LINUX_FILE_WRITENUM(LinuxFile_WriteUInt, UINT, WriteUInt)
LINUX_FILE_WRITENUM(LinuxFile_WriteUShort, UINT16, WriteUShort)

#undef LINUX_FILE_READNUM
#undef LINUX_FILE_WRITENUM

BIF_DECL(LinuxFile_ReadDouble)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		double ret = 0;
		FResult fr = (fo->*g_file_members.ReadDouble)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue(ret);
	}
}

BIF_DECL(LinuxFile_ReadFloat)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		float ret = 0;
		FResult fr = (fo->*g_file_members.ReadFloat)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((double)ret);
	}
}

BIF_DECL(LinuxFile_WriteDouble)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.WriteDouble)(TokenToDouble(*aParam[1]), ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_WriteFloat)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT ret = 0;
		FResult fr = (fo->*g_file_members.WriteFloat)((float)TokenToDouble(*aParam[1]), ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_GetAtEOF)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		BOOL ret = FALSE;
		FResult fr = (fo->*g_file_members.get_AtEOF)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue(ret ? 1 : 0);
	}
}

BIF_DECL(LinuxFile_GetHandle)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		UINT_PTR ret = 0;
		FResult fr = (fo->*g_file_members.get_Handle)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue((__int64)ret);
	}
}

BIF_DECL(LinuxFile_GetLength)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		__int64 ret = 0;
		FResult fr = (fo->*g_file_members.get_Length)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue(ret);
	}
}

BIF_DECL(LinuxFile_SetLength)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
		LinuxFileCheck(aResultToken, (fo->*g_file_members.set_Length)(TokenToInt64(*aParam[1])), aParam, aParamCount);
}

BIF_DECL(LinuxFile_GetPos)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
	{
		__int64 ret = 0;
		FResult fr = (fo->*g_file_members.get_Pos)(ret);
		if (FAILED(fr)) { LinuxFileCheck(aResultToken, fr, aParam, aParamCount); return; }
		aResultToken.SetValue(ret);
	}
}

BIF_DECL(LinuxFile_SetPos)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
		LinuxFileCheck(aResultToken, (fo->*g_file_members.set_Pos)(TokenToInt64(*aParam[1])), aParam, aParamCount);
}

BIF_DECL(LinuxFile_GetEncoding)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
		LinuxFileCheck(aResultToken, (fo->*g_file_members.get_Encoding)(aResultToken), aParam, aParamCount);
}

BIF_DECL(LinuxFile_SetEncoding)
{
	if (auto *fo = LinuxThisFile(aResultToken, aParam, aParamCount))
		LinuxFileCheck(aResultToken, (fo->*g_file_members.set_Encoding)(*aParam[1]), aParam, aParamCount);
}

} // namespace

// Called from Object::DefineMetadataMembers() for the "File" class.
void DefineFileClassLinuxOnPrototype(Object *aPrototype)
{
	if (!aPrototype)
		return;
	DefineFileClassLinux(); // Fill the member table (idempotent).

	// Methods (BuiltInFunc params include `this` as aParam[0]).
	#define F_METHOD(name, fn, minp, maxp) \
		aPrototype->DefineMethod(_T(name), new BuiltInFunc(_T(name), fn, (minp) + 1, (maxp) + 1))
	#define F_PROP_GET(name, fn) \
		{ auto prop = aPrototype->DefineProperty(_T(name)); \
		  auto func = new BuiltInFunc(_T(name ".Get"), fn, 1, 1); \
		  prop->SetGetter(func); prop->NoParamGet = true; func->Release(); }
	#define F_PROP_GETSET(name, fnget, fnset) \
		{ auto prop = aPrototype->DefineProperty(_T(name)); \
		  auto get = new BuiltInFunc(_T(name ".Get"), fnget, 1, 1); \
		  auto set = new BuiltInFunc(_T(name ".Set"), fnset, 2, 2); \
		  prop->SetGetter(get); get->Release(); \
		  prop->SetSetter(set); set->Release(); \
		  prop->NoParamGet = true; prop->NoParamSet = true; }

	F_METHOD("Close", LinuxFile_Close, 0, 0);
	F_METHOD("Read", LinuxFile_Read, 0, 1);
	F_METHOD("Write", LinuxFile_Write, 1, 1);
	F_METHOD("ReadLine", LinuxFile_ReadLine, 0, 0);
	F_METHOD("WriteLine", LinuxFile_WriteLine, 0, 1);
	F_METHOD("Seek", LinuxFile_Seek, 1, 2);
	F_METHOD("RawRead", LinuxFile_RawRead, 1, 2);
	F_METHOD("RawWrite", LinuxFile_RawWrite, 1, 2);
	F_METHOD("ReadChar", LinuxFile_ReadChar, 0, 0);
	F_METHOD("ReadInt", LinuxFile_ReadInt, 0, 0);
	F_METHOD("ReadInt64", LinuxFile_ReadInt64, 0, 0);
	F_METHOD("ReadShort", LinuxFile_ReadShort, 0, 0);
	F_METHOD("ReadUChar", LinuxFile_ReadUChar, 0, 0);
	F_METHOD("ReadUInt", LinuxFile_ReadUInt, 0, 0);
	F_METHOD("ReadUShort", LinuxFile_ReadUShort, 0, 0);
	F_METHOD("ReadDouble", LinuxFile_ReadDouble, 0, 0);
	F_METHOD("ReadFloat", LinuxFile_ReadFloat, 0, 0);
	F_METHOD("WriteChar", LinuxFile_WriteChar, 1, 1);
	F_METHOD("WriteInt", LinuxFile_WriteInt, 1, 1);
	F_METHOD("WriteInt64", LinuxFile_WriteInt64, 1, 1);
	F_METHOD("WriteShort", LinuxFile_WriteShort, 1, 1);
	F_METHOD("WriteUChar", LinuxFile_WriteUChar, 1, 1);
	F_METHOD("WriteUInt", LinuxFile_WriteUInt, 1, 1);
	F_METHOD("WriteUShort", LinuxFile_WriteUShort, 1, 1);
	F_METHOD("WriteDouble", LinuxFile_WriteDouble, 1, 1);
	F_METHOD("WriteFloat", LinuxFile_WriteFloat, 1, 1);
	F_PROP_GET("AtEOF", LinuxFile_GetAtEOF);
	F_PROP_GET("Handle", LinuxFile_GetHandle);
	F_PROP_GETSET("Length", LinuxFile_GetLength, LinuxFile_SetLength);
	F_PROP_GETSET("Pos", LinuxFile_GetPos, LinuxFile_SetPos);
	F_PROP_GETSET("Encoding", LinuxFile_GetEncoding, LinuxFile_SetEncoding);

	#undef F_METHOD
	#undef F_PROP_GET
	#undef F_PROP_GETSET
}

