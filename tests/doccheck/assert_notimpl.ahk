; NOT_IMPL module doc-check (v2 docs: functions not portable to Linux).
; COM D-Bus-unsupported calls raise their specific "not supported on Linux"
; messages.  (InputHook is now implemented - see assert_sound_etc.ahk.)
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

; --- ComObj functions unavailable on D-Bus ----------------------------------
Check("comobjarray", () => ComObjArray(0x0003, 2))
Check("comobjquery", () => ComObjQuery(ComValue(0x0003, 5), "{00000000-0000-0000-C000-000000000046}"))
Check("comobjconnect", () => ComObjConnect(ComObject("org.freedesktop.DBus")))

ExitApp 0