# HotkeyBackendGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:123](../../tests/doccheck/assert_hotkey.ahk#L123)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotstring.ahk:50](../../tests/doccheck/assert_hotstring.ahk#L50)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
bk := A_HotkeyBackend
Log("caps_backend_nn=" (bk != "" ? 1 : 0))
bo := HotkeyBackendGet()
Log("caps_obj_ok=" (IsObject(bo) && bo.backend != "" && (bo.global_hotkeys = 1 || bo.global_hotkeys = 0) ? 1 : 0))
Log("caps_schema_v2=" (bo.caps_version = 2 && bo.event_version = 1 ? 1 : 0))
Log("caps_extended_x11=" (bo.backend = "x11"
````
