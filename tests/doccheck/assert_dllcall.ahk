; DllCall doc-check: calls real .so functions via dlopen/dlsym + libffi.
; Docs: DllCall(DllFile & Function, Type1, Arg1, ..., ReturnType)
#Requires AutoHotkey v2.0

; --- Function-only form (searched in the process / all loaded objects) ---
MsgBox "abs=" (DllCall("abs", "Int", -42) = 42)
MsgBox "abs_pos=" (DllCall("abs", "Int", 7) = 7)
MsgBox "abs64=" (DllCall("labs", "Int64", -21474836480, "Int64") = 21474836480)
MsgBox "strlen=" (DllCall("strlen", "AStr", "hello") = 5)
MsgBox "strlen_utf8=" (DllCall("strlen", "AStr", "你好") = 6)
MsgBox "strlen_empty=" (DllCall("strlen", "AStr", "") = 0)
MsgBox "isdigit=" (DllCall("isdigit", "Int", Ord("5")) != 0)
MsgBox "isalpha=" (DllCall("isalpha", "Int", Ord("x")) != 0)

; --- DllFile & Function form (dlopen candidates: libX.so etc.) ---
MsgBox "abs_dl=" (DllCall("libc.so.6\abs", "Int", -5) = 5)
MsgBox "strlen_dl=" (DllCall("libc.so.6\strlen", "AStr", "abc") = 3)
MsgBox "floor_dl=" (DllCall("libm.so.6\floor", "Double", 3.7, "Double") = 3.0)
MsgBox "sqrt_dl=" (DllCall("libm.so.6\sqrt", "Double", 9.0, "Double") = 3.0)

; --- Return type variants ---
MsgBox "ret_short=" (DllCall("abs", "Short", -9) = 9)
MsgBox "ret_char=" (DllCall("abs", "Char", -3) = 3)
mptr := DllCall("malloc", "UInt", 16, "Ptr")
MsgBox "ret_ptr=" (mptr != 0)
DllCall("free", "Ptr", mptr)
MsgBox "ret_str=" (DllCall("getenv", "AStr", "HOME", "AStr") != "")

; --- Argument types ---
MsgBox "arg_int64=" (DllCall("llabs", "Int64", -9000000000, "Int64") = 9000000000)
MsgBox "arg_double=" (DllCall("pow", "Double", 2.0, "Double", 10.0, "Double") = 1024.0)
MsgBox "arg_float=" (DllCall("ceil", "Double", 2.1, "Double") = 3.0)
MsgBox "arg_uchar=" (DllCall("abs", "UChar", -5) = 5)

; --- By-address output parameters (require &Var, like upstream v2) ---
; time(time_t *t) writes the current time through its pointer argument.
t_out := 0
DllCall("time", "Int64*", &t_out)
MsgBox "out_time=" (t_out > 1000000000)

; rand_r(unsigned int *seedp) updates the seed through its pointer argument.
seed := 1
DllCall("rand_r", "UInt*", &seed)
MsgBox "out_seed_changed=" (seed != 1)

; Ptr return value (malloc/free round trip).
ptr := DllCall("malloc", "UInt", 32, "Ptr")
MsgBox "ret_ptr_nonzero=" (ptr != 0)
DllCall("free", "Ptr", ptr)

; Plain variable (no &) is input-only: not written back.
in_only := 99
DllCall("time", "Int64*", in_only)
MsgBox "in_only_unchanged=" (in_only = 99)

; Buffer-backed output.
b := Buffer(8)
DllCall("sprintf", "Ptr", b.Ptr, "AStr", "%d-%d", "Int", 3, "Int", 4)
MsgBox "out_buf=" (StrGet(b, 3, "UTF-8") = "3-4")
; --- Failure cases ---
try
    DllCall("no_such_function_xyz")
catch
    MsgBox "missing_fn_err=1"
try
    DllCall("libno_such_lib_xyz.so\fn")
catch
    MsgBox "missing_dll_err=1"
try
    DllCall("user32.dll\MessageBoxW")
catch as e
    MsgBox "windows_dll_err=" (InStr(e.Message, "Windows DLL is not available") ? 1 : 0)
try
    DllCall("user32\MessageBoxW")
catch as e
    MsgBox "windows_lib_err=" (InStr(e.Message, "Windows DLL is not available") ? 1 : 0)
try
    DllCall("abs", "BogusType", 1)
catch
    MsgBox "bad_type_err=1"
try
    DllCall("abs", "Int", 1, "NoSuchType")
catch
    MsgBox "bad_ret_err=1"
