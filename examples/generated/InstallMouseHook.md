# InstallMouseHook

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:341](../../tests/doccheck/assert_input.ahk#L341)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Records a boolean only; no low-level hook is installed

Installs or uninstalls the mouse hook.

## Syntax

````text
InstallMouseHook Install, Force
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- InstallKeybdHook/InstallMouseHook: no error, flags stored. ---
InstallKeybdHook()
InstallMouseHook()
InstallKeybdHook(0)
InstallMouseHook(0)
Log("install_hooks=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/InstallMouseHook.htm](../../docs-v2/docs/lib/InstallMouseHook.htm)

````ahk
InstallMouseHook
````
