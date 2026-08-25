# SubStr

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:67](../../tests/doccheck/assert_clipboard.ahk#L67)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `desktop-session`: [tests/doccheck/assert_ime.ahk:23](../../tests/doccheck/assert_ime.ahk#L23)
- `headless`: [tests/doccheck/assert_string.ahk:7](../../tests/doccheck/assert_string.ahk#L7)
- `headless`: [tests/doccheck/assert_sys.ahk:154](../../tests/doccheck/assert_sys.ahk#L154)
- `x11`: [tests/doccheck/assert_win.ahk:53](../../tests/doccheck/assert_win.ahk#L53)

Retrieves one or more characters from the specified position in a string.

## Syntax

````text
NewStr := SubStr(String, StartingPos , Length)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
got := probe_run("--get")
Log("clip_big_len=" (StrLen(got) = StrLen(big) ? 1 : 0))
Log("clip_big_head=" (SubStr(got, 1, 20) = SubStr(big, 1, 20) ? 1 : 0))
Log("clip_big_tail=" (SubStr(got, -20) = SubStr(big, -20) ? 1 : 0))

; --- External slow owner: xclip_probe --set takes ownership and serves
````

## Upstream reference example

Source: [docs-v2/docs/lib/SubStr.htm](../../docs-v2/docs/lib/SubStr.htm)

````ahk
MsgBox SubStr("123abc789", 4, 3) ; Returns abc
````
