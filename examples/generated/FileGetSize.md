# FileGetSize

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:29](../../tests/doccheck/assert_ctrl.ahk#L29)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_file.ahk:58](../../tests/doccheck/assert_file.ahk#L58)
- `x11`: [tests/doccheck/assert_input.ahk:27](../../tests/doccheck/assert_input.ahk#L27)
- `x11`: [tests/doccheck/assert_layout.ahk:23](../../tests/doccheck/assert_layout.ahk#L23)
- `x11`: [tests/doccheck/assert_repeat.ahk:30](../../tests/doccheck/assert_repeat.ahk#L30)
- `headless`: [tests/doccheck/assert_sys.ahk:313](../../tests/doccheck/assert_sys.ahk#L313)

Retrieves the size of a file.

## Syntax

````text
Size := FileGetSize(Filename, Units)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    rest := f.Read()
    f.Close()
    prev_bytes := FileGetSize(EVOUT)
    return StrSplit(rest, "`n")
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileGetSize.htm](../../docs-v2/docs/lib/FileGetSize.htm)

````ahk
Size := FileGetSize("C:\My Documents\test.doc")
````
