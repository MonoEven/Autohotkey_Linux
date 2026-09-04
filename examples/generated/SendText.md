# SendText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotstring.ahk:43](../../tests/doccheck/assert_hotstring.ahk#L43)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_input.ahk:89](../../tests/doccheck/assert_input.ahk#L89)
- `x11`: [tests/doccheck/assert_inputhook.ahk:103](../../tests/doccheck/assert_inputhook.ahk#L103)
- `x11`: [tests/doccheck/assert_repeat.ahk:52](../../tests/doccheck/assert_repeat.ahk#L52)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:27](../../tests/doccheck/assert_unicode_lease.ahk#L27)
- `wayland`: [tests/doccheck/assert_wayland.ahk:60](../../tests/doccheck/assert_wayland.ahk#L60)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Send("abz")                 ; -> "W" + 'z'.
Send("hello ")              ; no match -> passthrough.
SendText("你好")             ; CJK raw trigger -> Backspace x2 + "nn".
Send("gh")                   ; B0 -> original "gh" remains, then R.
Send("xij ")                ; inside-word guard: no replacement.
Send(" ij ")                ; boundary match: Backspace x3 + S + space.
````
