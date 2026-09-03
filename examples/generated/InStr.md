# InStr

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:41](../../tests/doccheck/assert_clipboard.ahk#L41)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:28](../../tests/doccheck/assert_clipboard_slow.ahk#L28)
- `dbus`: [tests/doccheck/assert_com.ahk:68](../../tests/doccheck/assert_com.ahk#L68)
- `headless`: [tests/doccheck/assert_diag.ahk:16](../../tests/doccheck/assert_diag.ahk#L16)
- `headless`: [tests/doccheck/assert_dllcall.ahk:72](../../tests/doccheck/assert_dllcall.ahk#L72)
- `x11`: [tests/doccheck/assert_edit.ahk:113](../../tests/doccheck/assert_edit.ahk#L113)
- `headless`: [tests/doccheck/assert_file.ahk:62](../../tests/doccheck/assert_file.ahk#L62)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:87](../../tests/doccheck/assert_hotkey_btn.ahk#L87)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:87](../../tests/doccheck/assert_hotkey_lr.ahk#L87)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:94](../../tests/doccheck/assert_hotkey_pt.ahk#L94)
- `x11`: [tests/doccheck/assert_hotstring.ahk:58](../../tests/doccheck/assert_hotstring.ahk#L58)
- `desktop-session`: [tests/doccheck/assert_ime.ahk:22](../../tests/doccheck/assert_ime.ahk#L22)
- `x11`: [tests/doccheck/assert_input.ahk:241](../../tests/doccheck/assert_input.ahk#L241)
- `x11`: [tests/doccheck/assert_inputhook.ahk:148](../../tests/doccheck/assert_inputhook.ahk#L148)
- `headless`: [tests/doccheck/assert_notimpl.ahk:17](../../tests/doccheck/assert_notimpl.ahk#L17)
- `headless`: [tests/doccheck/assert_parity.ahk:13](../../tests/doccheck/assert_parity.ahk#L13)
- `x11`: [tests/doccheck/assert_repeat.ahk:89](../../tests/doccheck/assert_repeat.ahk#L89)
- `x11`: [tests/doccheck/assert_shape.ahk:41](../../tests/doccheck/assert_shape.ahk#L41)
- `headless`: [tests/doccheck/assert_strict.ahk:14](../../tests/doccheck/assert_strict.ahk#L14)
- `headless`: [tests/doccheck/assert_string.ahk:12](../../tests/doccheck/assert_string.ahk#L12)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:54](../../tests/doccheck/assert_unicode_lease.ahk#L54)
- `wayland`: [tests/doccheck/assert_wayland.ahk:82](../../tests/doccheck/assert_wayland.ahk#L82)

Searches for a given occurrence of a string, from the left or the right.

## Syntax

````text
FoundPos := InStr(Haystack, Needle , CaseSense, StartingPos, Occurrence)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(150)
t := probe_run("--targets")
Log("clip_targets_utf8=" (InStr(t, "UTF8_STRING") ? 1 : 0))
Log("clip_targets_string=" (InStr(t, "STRING") ? 1 : 0))
Log("clip_targets_has_targets=" (InStr(t, "TARGETS") ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/InStr.htm](../../docs-v2/docs/lib/InStr.htm)

````ahk
MsgBox InStr("123abc789", "abc") ; Returns 4
````
