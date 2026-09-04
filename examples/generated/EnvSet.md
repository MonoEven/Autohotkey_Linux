# EnvSet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:101](../../tests/doccheck/assert_ctrl.ahk#L101)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_dialog.ahk:15](../../tests/doccheck/assert_dialog.ahk#L15)
- `x11`: [tests/doccheck/assert_edit.ahk:107](../../tests/doccheck/assert_edit.ahk#L107)
- `headless`: [tests/doccheck/assert_general.ahk:5](../../tests/doccheck/assert_general.ahk#L5)
- `headless`: [tests/doccheck/assert_registry.ahk:7](../../tests/doccheck/assert_registry.ahk#L7)
- `headless`: [tests/doccheck/assert_strict.ahk:7](../../tests/doccheck/assert_strict.ahk#L7)
- `headless`: [tests/doccheck/assert_sys.ahk:11](../../tests/doccheck/assert_sys.ahk#L11)

Writes a value to the specified environment variable.

## Syntax

````text
EnvSet EnvVar , Value
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
old_desktop := EnvGet("XDG_CURRENT_DESKTOP")
old_wl := EnvGet("WAYLAND_DISPLAY")
EnvSet("XDG_SESSION_TYPE", "x11")
EnvSet("XDG_CURRENT_DESKTOP", "GNOME")
EnvSet("WAYLAND_DISPLAY", "wayland-leaked")
Log("gnome_x11_control=" (ControlGetText("Edit1", "CtlMain") = "Edit1" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/EnvSet.htm](../../docs-v2/docs/lib/EnvSet.htm)

````ahk
EnvSet "AutGUI", "Some text to put in the environment variable."
````
