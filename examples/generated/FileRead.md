# FileRead

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:31](../../tests/doccheck/assert_clipboard.ahk#L31)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_change.ahk:42](../../tests/doccheck/assert_clipboard_change.ahk#L42)
- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:27](../../tests/doccheck/assert_clipboard_slow.ahk#L27)
- `headless`: [tests/doccheck/assert_diag.ahk:14](../../tests/doccheck/assert_diag.ahk#L14)
- `x11`: [tests/doccheck/assert_edit.ahk:111](../../tests/doccheck/assert_edit.ahk#L111)
- `headless`: [tests/doccheck/assert_file.ahk:18](../../tests/doccheck/assert_file.ahk#L18)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:86](../../tests/doccheck/assert_hotkey_btn.ahk#L86)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:86](../../tests/doccheck/assert_hotkey_lr.ahk#L86)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:93](../../tests/doccheck/assert_hotkey_pt.ahk#L93)
- `x11`: [tests/doccheck/assert_hotstring.ahk:57](../../tests/doccheck/assert_hotstring.ahk#L57)
- `x11`: [tests/doccheck/assert_inputhook.ahk:147](../../tests/doccheck/assert_inputhook.ahk#L147)
- `headless`: [tests/doccheck/assert_parity.ahk:12](../../tests/doccheck/assert_parity.ahk#L12)
- `x11`: [tests/doccheck/assert_repeat.ahk:89](../../tests/doccheck/assert_repeat.ahk#L89)
- `x11`: [tests/doccheck/assert_shape.ahk:34](../../tests/doccheck/assert_shape.ahk#L34)
- `headless`: [tests/doccheck/assert_statements.ahk:134](../../tests/doccheck/assert_statements.ahk#L134)
- `headless`: [tests/doccheck/assert_sys.ahk:141](../../tests/doccheck/assert_sys.ahk#L141)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:60](../../tests/doccheck/assert_unicode_lease.ahk#L60)
- `wayland`: [tests/doccheck/assert_wayland.ahk:82](../../tests/doccheck/assert_wayland.ahk#L82)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:49](../../tests/doccheck/assert_clipboard_all.ahk#L49)

Retrieves the contents of a file.

## Syntax

````text
Text := FileRead(Filename , Options)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
        i += 1
    }
    t := FileExist(TBASE ".txt") ? FileRead(TBASE ".txt") : ""
    return RTrim(t, "`r`n")   ; probe appends a trailing newline
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileRead.htm](../../docs-v2/docs/lib/FileRead.htm)

````ahk
MyText := FileRead("C:\My Documents\My File.txt")
````
