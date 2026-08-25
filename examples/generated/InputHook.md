# InputHook

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:215](../../tests/doccheck/assert_input.ahk#L215)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_inputhook.ahk:20](../../tests/doccheck/assert_inputhook.ahk#L20)
- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:48](../../tests/doccheck/assert_sound_etc.ahk#L48)

Creates an object which can be used to collect or intercept keyboard input.

## Syntax

````text
InputHookObj := InputHook(Options, EndKeys, MatchList)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; is ignored.  Physical input always feeds (not exercised here).
SendLevel(2)
ih_hi := InputHook("I1", "z")   ; I1 = MinSendLevel 1; end key z.
ih_hi.Start()
Sleep(100)
Send("a")        ; level 2 >= 1 -> collected.
````

## Upstream reference example

Source: [docs-v2/docs/lib/InputHook.htm](../../docs-v2/docs/lib/InputHook.htm)

````ahk
MsgBox KeyWaitAny()
; Same again, but don't block the key.
MsgBox KeyWaitAny("V")
KeyWaitAny(Options:="")
{
    ih := InputHook(Options)
    if !InStr(Options, "V")
        ih.VisibleNonText := false
    ih.KeyOpt("{All}", "E")  ; End
    ih.Start()
    ih.Wait()
    return ih.
````
