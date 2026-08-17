; NOT_IMPL module doc-check (v2 docs: functions not portable to Linux).
; Verifies that calling each not-ported function raises a clear runtime
; error (NOT silently return garbage, crash, or report a misleading error):
;   - stubbed g_BIF functions / LMD_NI / class ctors: the standard
;     "This built-in function has not been ported to Linux yet."
;   - COM D-Bus-unsupported calls: their specific "not supported on Linux"
;     messages.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_notimpl_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

Check(name, fn) {
    try {
        r := fn()
        Log(name "=NO_ERROR:" Type(r))
    } catch as e {
        if e.Message = "This built-in function has not been ported to Linux yet."
            Log(name "=notimpl")
        else if InStr(e.Message, "COM:") && InStr(e.Message, "not supported on Linux")
            Log(name "=com_err")
        else
            Log(name "=OTHER:" e.Message)
    }
}

; --- stubbed g_BIF functions (core_builtin_stubs.cpp) ----------------------
Check("caretgetpos", () => CaretGetPos(&x, &y))
Check("soundgetmute", () => SoundGetMute())
Check("soundgetvolume", () => SoundGetVolume())
Check("soundgetname", () => SoundGetName())
Check("soundsetmute", () => SoundSetMute(1))
Check("soundsetvolume", () => SoundSetVolume(50))
Check("soundgetinterface", () => SoundGetInterface("{00000000-0000-0000-C000-000000000046}"))

; --- LMD_NI entries (core_mdfunc_linux.cpp) -------------------------------
Check("callbackcreate", () => CallbackCreate((*) => 0))
Check("callbackfree", () => CallbackFree(0))

; --- class constructors not ported ------------------------------------------
Check("inputhook", () => InputHook())

; --- ComObj functions unavailable on D-Bus ----------------------------------
Check("comobjarray", () => ComObjArray(0x0003, 2))
Check("comobjquery", () => ComObjQuery(ComValue(0x0003, 5), "{00000000-0000-0000-C000-000000000046}"))
Check("comobjconnect", () => ComObjConnect(ComObject("org.freedesktop.DBus")))

ExitApp 0