# SetStoreCapsLockMode

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:145](../../tests/doccheck/assert_sys.ahk#L145)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Whether to restore the state of the CapsLock key after sending simulated keystrokes.

## Syntax

````text
PrevSetting := SetStoreCapsLockMode(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: SetStoreCapsLockMode returns the previous setting (default On).
MsgBox "StoreCapsLockMode_prev=" (SetStoreCapsLockMode(0) = 1)
MsgBox "StoreCapsLockMode_set=" (A_StoreCapsLockMode = 0)
MsgBox "StoreCapsLockMode_return=" (SetStoreCapsLockMode(1) = 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetStoreCapsLockMode.htm](../../docs-v2/docs/lib/SetStoreCapsLockMode.htm)

````ahk
SetStoreCapsLockMode False
````
