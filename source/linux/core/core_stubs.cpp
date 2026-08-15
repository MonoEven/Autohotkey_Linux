// Temporary Linux stubs for core error-reporting functions.
// These will be replaced by real error handling as the interpreter is ported.

#include "../../stdafx.h"
#include "../core_errors.h"
#include "../../SimpleHeap.h"
#include "../../script.h"
#include "../../os_version.h"
#include <cstdio>
#include <cstdlib>

ResultType MemoryError()
{
	std::fputs("AutoHotkey Linux: out of memory\n", stderr);
	return FAIL;
}

void SimpleHeap::CriticalFail()
{
	std::fputs("AutoHotkey Linux: fatal out-of-memory error\n", stderr);
	std::exit(1);
}

// Temporary stubs for global objects until the real implementations are ported.
Script::Script()
{
}

Script::~Script()
{
}

void OS_Version::Init()
{
	m_dwMajorVersion = 6;
	m_dwMinorVersion = 8;
	m_dwBuildNumber = 0;
	m_bWinVista = true;
	m_bWinVistaOrLater = true;
	m_bWin7 = true;
	m_bWin7OrLater = true;
	m_bWin8 = true;
	m_bWin8_1 = true;
}

// Temporary stubs for the object model and token helpers.
Object *Object::sPrototype = nullptr;
Object *Object::sClass = nullptr;
Object *Object::sClassPrototype = nullptr;
IObject *Object::sObjectCall = nullptr;

Object::~Object()
{
}

bool Object::Delete()
{
	return false;
}

ResultType ObjectBase::Invoke(ResultToken &, int, LPTSTR, ExprTokenType &, ExprTokenType *[], int)
{
	return FAIL;
}

bool ObjectBase::IsOfType(Object *)
{
	return false;
}

ResultType Object::Invoke(ResultToken &, int, LPTSTR, ExprTokenType &, ExprTokenType *[], int)
{
	return FAIL;
}

LPTSTR Object::Type()
{
	return _T("Object");
}

bool Object::IsOfType(Object *)
{
	return false;
}

Object *Object::CreatePrototype(LPTSTR, Object *, ObjectMemberMd *, int)
{
	return nullptr;
}

Object *Object::CreateClass(LPTSTR, Object *, Object *, ClassFactoryDef)
{
	return nullptr;
}

SymbolType TokenIsNumeric(ExprTokenType &)
{
	return SYM_STRING;
}

__int64 TokenToInt64(ExprTokenType &)
{
	return 0;
}

LPTSTR TokenToString(ExprTokenType &, LPTSTR, size_t *)
{
	return nullptr;
}

IObject *TokenToObject(ExprTokenType &)
{
	return nullptr;
}

SymbolType TypeOfToken(ExprTokenType &)
{
	return SYM_STRING;
}

FResult FParamError(int, ExprTokenType *, LPCTSTR)
{
	return FR_E_ARG(1);
}

void GetBufferObjectPtr(ResultToken &, IObject *, size_t &, size_t &)
{
}

ResultType ResultToken::Win32Error(DWORD)
{
	return FAIL;
}

ResultType ResultToken::ValueError(LPCTSTR)
{
	return FAIL;
}

void Object::Variant::Free()
{
}


