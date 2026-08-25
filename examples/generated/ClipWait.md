# ClipWait

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:93](../../tests/doccheck/assert_clipboard.ahk#L93)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:219](../../tests/doccheck/assert_misc_cov.ahk#L219)

Waits until the clipboard contains data.

## Syntax

````text
Boolean := ClipWait(Timeout, WaitFor)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(150)
t0 := A_TickCount
w := ClipWait(0.5)
Log("clip_clipwait_ok=" ((w = 1 && A_TickCount - t0 < 500) ? 1 : 0))

A_Clipboard := ""
````

## Upstream reference example

Source: [docs-v2/docs/lib/ClipWait.htm](../../docs-v2/docs/lib/ClipWait.htm)

````ahk
A_Clipboard := "" ; Empty the clipboard
Send "^c"
if !ClipWait(2)
{
    MsgBox "The attempt to copy text onto the clipboard failed."
    return
}
MsgBox "clipboard = " A_Clipboard
return
````
