# Integer

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey_pt.ahk:108](../../tests/doccheck/assert_hotkey_pt.ahk#L108)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `desktop-session`: [tests/doccheck/assert_ime.ahk:26](../../tests/doccheck/assert_ime.ahk#L26)
- `x11`: [tests/doccheck/assert_input.ahk:125](../../tests/doccheck/assert_input.ahk#L125)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:47](../../tests/doccheck/assert_misc_cov.ahk#L47)

Converts a numeric string or floating-point value to an integer.

## Syntax

````text
IntValue := Integer(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    i += 1
}
cpid := FileExist("/tmp/ahk_kill9.pid") ? Integer(FileRead("/tmp/ahk_kill9.pid")) : 0
if cpid
    RunWait('kill -9 ' cpid)
RunWait("pkill -9 -f ahk_kill9_child")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Integer.htm](../../docs-v2/docs/lib/Integer.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
