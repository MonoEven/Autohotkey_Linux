# ClipboardAll

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard_all.ahk:129](../../tests/doccheck/assert_clipboard_all.ahk#L129)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:222](../../tests/doccheck/assert_misc_cov.ahk#L222)

Creates an object containing everything on the clipboard (such as pictures and formatting).

## Syntax

````text
ClipSaved := ClipboardAll(Data, Size)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    NumPut("UInt", 0x08000001, raw, 15) ; declared data length
    NumPut("UInt", 0, raw, 19)          ; checksum (never reached)
    return ClipboardAll(raw, raw.Size)
}

; Start the external owner asynchronously.  Waiting is only for its explicit
````

## Upstream reference example

Source: [docs-v2/docs/lib/ClipboardAll.htm](../../docs-v2/docs/lib/ClipboardAll.htm)

````ahk
ClipSaved := ClipboardAll()   ; Save the entire clipboard to a variable of your choice.
; ... here make temporary use of the clipboard, such as for quickly pasting large amounts of text ...
A_Clipboard := ClipSaved   ; Restore the original clipboard. Note the use of A_Clipboard (not ClipboardAll
````
