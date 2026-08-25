# StrLen

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:66](../../tests/doccheck/assert_clipboard.ahk#L66)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `dbus`: [tests/doccheck/assert_com.ahk:37](../../tests/doccheck/assert_com.ahk#L37)
- `headless`: [tests/doccheck/assert_datetime.ahk:29](../../tests/doccheck/assert_datetime.ahk#L29)
- `headless`: [tests/doccheck/assert_file.ahk:49](../../tests/doccheck/assert_file.ahk#L49)
- `x11`: [tests/doccheck/assert_msg.ahk:165](../../tests/doccheck/assert_msg.ahk#L165)
- `headless`: [tests/doccheck/assert_string.ahk:5](../../tests/doccheck/assert_string.ahk#L5)

Retrieves the count of how many characters are in a string.

## Syntax

````text
Length := StrLen(String)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(150)
got := probe_run("--get")
Log("clip_big_len=" (StrLen(got) = StrLen(big) ? 1 : 0))
Log("clip_big_head=" (SubStr(got, 1, 20) = SubStr(big, 1, 20) ? 1 : 0))
Log("clip_big_tail=" (SubStr(got, -20) = SubStr(big, -20) ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrLen.htm](../../docs-v2/docs/lib/StrLen.htm)

````ahk
StrValue := "The quick brown fox jumps over the lazy dog"
MsgBox "The length of the string is " StrLen(StrValue) ; Result: 43
````
