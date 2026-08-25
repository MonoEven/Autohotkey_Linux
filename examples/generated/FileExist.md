# FileExist

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:27](../../tests/doccheck/assert_clipboard.ahk#L27)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:23](../../tests/doccheck/assert_clipboard_slow.ahk#L23)
- `x11`: [tests/doccheck/assert_edit.ahk:110](../../tests/doccheck/assert_edit.ahk#L110)
- `headless`: [tests/doccheck/assert_file.ahk:5](../../tests/doccheck/assert_file.ahk#L5)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:104](../../tests/doccheck/assert_hotkey_pt.ahk#L104)
- `x11`: [tests/doccheck/assert_shape.ahk:32](../../tests/doccheck/assert_shape.ahk#L32)
- `headless`: [tests/doccheck/assert_statements.ahk:132](../../tests/doccheck/assert_statements.ahk#L132)
- `headless`: [tests/doccheck/assert_sys.ahk:224](../../tests/doccheck/assert_sys.ahk#L224)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:32](../../tests/doccheck/assert_unicode_lease.ahk#L32)
- `wayland`: [tests/doccheck/assert_wayland.ahk:41](../../tests/doccheck/assert_wayland.ahk#L41)

Checks for the existence of a file or folder and returns its attributes.

## Syntax

````text
AttributeString := FileExist(FilePattern)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    RunWait(PROBE ' ' opts ' -out ' TBASE '.txt')
    i := 0
    while !FileExist(TBASE ".txt") && i < 60 {
        Sleep(50)
        i += 1
    }
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileExist.htm](../../docs-v2/docs/lib/FileExist.htm)

````ahk
if FileExist("D:\")
    MsgBox "The drive exists."
````
