# CallbackCreate

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:42](../../tests/doccheck/assert_sound_etc.ahk#L42)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Creates a machine-code address that when called, redirects the call to a function in the script.

## Syntax

````text
Address := CallbackCreate(Function , Options, ParamCount)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- CallbackCreate / CallbackFree ------------------------------------------
cb := CallbackCreate((*) => 42, "C", 0)
Log("cbaddr=" (IsInteger(cb) && cb > 0 ? "ok" : "bad"))
CallbackFree(cb)
Log("cbfree=ok")
````

## Upstream reference example

Source: [docs-v2/docs/lib/CallbackCreate.htm](../../docs-v2/docs/lib/CallbackCreate.htm)

````ahk
EnumAddress := CallbackCreate(EnumWindowsProc, "Fast")  ; Fast-mode is okay because it will be called only from this thread.
DetectHiddenWindows True  ; Due to fast-mode, this setting will go into effect for the callback too.
; Pass control to EnumWindows(), which calls the callback repeatedly:
````
