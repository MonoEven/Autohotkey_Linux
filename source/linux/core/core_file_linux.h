// Linux port: shared table used to register the File class members without
// the DynaCall x64 marshaler (MdFunc).  FileObject itself is defined in
// TextIO.cpp, so the friend function DefineFileClassLinux() lives there and
// fills this table; the BIF wrappers in core_file_linux.cpp call through it
// using Object::* member pointers (FileObject derives from Object).
#pragma once

#include "../../script_object.h"

struct FileMemberTable
{
	FResult (Object::*Close)();
	FResult (Object::*Read)(optl<UINT>, StrRet &);
	FResult (Object::*Write)(ExprTokenType &, UINT &);
	FResult (Object::*ReadLine)(StrRet &);
	FResult (Object::*WriteLine)(ExprTokenType *, UINT &);
	FResult (Object::*Seek)(__int64, optl<int>, BOOL &);
	FResult (Object::*RawRead)(ExprTokenType &, optl<UINT>, UINT &);
	FResult (Object::*RawWrite)(ExprTokenType &, optl<UINT>, UINT &);
	FResult (Object::*ReadChar)(INT8 &);
	FResult (Object::*ReadInt)(int &);
	FResult (Object::*ReadInt64)(__int64 &);
	FResult (Object::*ReadShort)(INT16 &);
	FResult (Object::*ReadUChar)(UINT8 &);
	FResult (Object::*ReadUInt)(UINT &);
	FResult (Object::*ReadUShort)(UINT16 &);
	FResult (Object::*ReadDouble)(double &);
	FResult (Object::*ReadFloat)(float &);
	FResult (Object::*WriteChar)(INT8, UINT &);
	FResult (Object::*WriteInt)(int, UINT &);
	FResult (Object::*WriteInt64)(__int64, UINT &);
	FResult (Object::*WriteShort)(INT16, UINT &);
	FResult (Object::*WriteUChar)(UINT8, UINT &);
	FResult (Object::*WriteUInt)(UINT, UINT &);
	FResult (Object::*WriteUShort)(UINT16, UINT &);
	FResult (Object::*WriteDouble)(double, UINT &);
	FResult (Object::*WriteFloat)(float, UINT &);
	FResult (Object::*get_AtEOF)(BOOL &);
	FResult (Object::*get_Handle)(UINT_PTR &);
	FResult (Object::*get_Length)(__int64 &);
	FResult (Object::*set_Length)(__int64);
	FResult (Object::*get_Pos)(__int64 &);
	FResult (Object::*set_Pos)(__int64);
	FResult (Object::*get_Encoding)(ResultToken &);
	FResult (Object::*set_Encoding)(ExprTokenType &);
};

extern FileMemberTable g_file_members;

// Friend of FileObject (TextIO.cpp): fills g_file_members.  Idempotent.
void DefineFileClassLinux();

// True if aObj is a FileObject instance (TextIO.cpp; FileObject is not
// visible outside TextIO.cpp).
bool LinuxIsFileObject(IObject *aObj);

// Called from Object::DefineMetadataMembers() for the "File" class.
void DefineFileClassLinuxOnPrototype(Object *aPrototype);
