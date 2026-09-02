# InstallKeybdHook

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:348](../../tests/doccheck/assert_input.ahk#L348)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Records a boolean only; no low-level hook is installed

## Additional verified environments

- `headless`: [tests/doccheck/assert_strict.ahk:11](../../tests/doccheck/assert_strict.ahk#L11)

Installs or uninstalls the keyboard hook.

## Syntax

````text
InstallKeybdHook Install, Force
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- InstallKeybdHook/InstallMouseHook: no error, flags stored. ---
InstallKeybdHook()
InstallMouseHook()
InstallKeybdHook(0)
InstallMouseHook(0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/InstallKeybdHook.htm](../../docs-v2/docs/lib/InstallKeybdHook.htm)

````ahk
InstallKeybdHook
````
