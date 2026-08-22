// core_mdfunc_linux.h -- Linux builtin-function construction helpers.
#pragma once
#include "../../stdafx.h"

struct FuncEntry;
class Func;

// Create a builtin Func from a FuncEntry (g_BIF or the Linux table), wrapping
// it with the strict-parity (AHK_STRICT_PARITY=warn|error) first-call check
// for P3/P4 functions (check_detail0821 §13).  Implemented in
// core_mdfunc_linux.cpp; Script::GetBuiltInFunc and
// Script::GetBuiltInMdFunc route through it so BOTH the upstream g_BIF table
// (RegRead, ComObjArray, ...) and the Linux table are covered.
Func *LinuxNewBuiltInFunc(FuncEntry &aFe);
