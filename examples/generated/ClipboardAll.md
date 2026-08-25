# ClipboardAll

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:222](../../tests/doccheck/assert_misc_cov.ahk#L222)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Creates an object containing everything on the clipboard (such as pictures and formatting).

## Syntax

````text
ClipSaved := ClipboardAll(Data, Size)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ClipAll() {
    b := Buffer(8, 0x41)
    ca := ClipboardAll(b, 8)
    return Type(ca)
}
Check("clipboardall", ClipAll)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ClipboardAll.htm](../../docs-v2/docs/lib/ClipboardAll.htm)

````ahk
ClipSaved := ClipboardAll()   ; Save the entire clipboard to a variable of your choice.
; ... here make temporary use of the clipboard, such as for quickly pasting large amounts of text ...
A_Clipboard := ClipSaved   ; Restore the original clipboard. Note the use of A_Clipboard (not ClipboardAll
````
