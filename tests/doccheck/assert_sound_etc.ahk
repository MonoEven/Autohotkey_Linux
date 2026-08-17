; Newly-implemented functions doc-check: Sound*, CaretGetPos, CallbackCreate.
; These run headless (no display / no audio server in CI), so they verify:
;   - SoundGet*/SoundSet* raise OSError when no mixer tool is present
;     (docs: exceptions on failure; matching SoundPlay), and
;     SoundGetInterface returns a numeric 0 (no COM audio on Linux).
;   - CaretGetPos returns 0 with blank output vars when there is no focused
;     GTK window (docs: "cannot be determined").
;   - CallbackCreate returns a numeric address > 0; CallbackFree accepts it.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_soundetc_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

; Expects an OSError; logs "ose" if so.
ExpectOSError(name, fn) {
    try {
        r := fn()
        Log(name "=no_error:" r)
    } catch as e {
        Log(name "=" (e is OSError ? "ose" : "other"))
    }
}

; --- SoundGetInterface returns 0 (no COM on Linux) --------------------------
Log("sgi=" (IsInteger(SoundGetInterface("{00000000-0000-0000-C000-000000000046}")) ? "ok" : "bad"))
Log("sgi2=" (SoundGetInterface("{}") = 0 ? "ok" : "bad"))

; --- SoundGet*/Set* without a mixer tool -> OSError -------------------------
ExpectOSError("sgm", () => SoundGetMute())
ExpectOSError("sgv", () => SoundGetVolume())
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
ExpectOSError("ssv", () => SoundSetVolume(50))

; --- CaretGetPos without a focused GTK window -------------------------------
x := y := "?"
ok := CaretGetPos(&x, &y)
Log("cgp=" (ok = 0 && x = "" && y = "" ? "ok" : "bad:" ok))

; --- CallbackCreate / CallbackFree ------------------------------------------
cb := CallbackCreate((*) => 42, "C", 0)
Log("cbaddr=" (IsInteger(cb) && cb > 0 ? "ok" : "bad"))
CallbackFree(cb)
Log("cbfree=ok")

; --- InputHook state machine (no live capture on Linux) ----------------------
ih := InputHook("T1")
Log("ih_type=" Type(ih))
Log("ih_start=" ih.InProgress)
ih.Start()
Sleep(1300)  ; longer than the 1s timeout; MsgSleep fires the timeout
Log("ih_after=" (ih.InProgress ? "active" : "done"))
Log("ih_reason=" ih.EndReason)

ih2 := InputHook()
ih2.Start()
ih2.Stop()
Log("ih2_stop=" (ih2.EndReason = "Stopped" ? "ok" : "bad"))

ExitApp 0