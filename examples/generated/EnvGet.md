# EnvGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:98](../../tests/doccheck/assert_ctrl.ahk#L98)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_dialog.ahk:64](../../tests/doccheck/assert_dialog.ahk#L64)
- `headless`: [tests/doccheck/assert_general.ahk:6](../../tests/doccheck/assert_general.ahk#L6)
- `x11`: [tests/doccheck/assert_layout.ahk:44](../../tests/doccheck/assert_layout.ahk#L44)

Retrieves the value of the specified environment variable.

## Syntax

````text
Value := EnvGet(EnvVar)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; check_detail0824 M0-B: GNOME desktop name and a leaked WAYLAND_DISPLAY must
; not override an explicit XDG_SESSION_TYPE=x11 session.
old_st := EnvGet("XDG_SESSION_TYPE")
old_desktop := EnvGet("XDG_CURRENT_DESKTOP")
old_wl := EnvGet("WAYLAND_DISPLAY")
EnvSet("XDG_SESSION_TYPE", "x11")
````

## Upstream reference example

Source: [docs-v2/docs/lib/EnvGet.htm](../../docs-v2/docs/lib/EnvGet.htm)

````ahk
LogonServer := EnvGet("LogonServer")
````
