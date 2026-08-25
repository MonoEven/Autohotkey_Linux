# Persistent

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:170](../../tests/doccheck/assert_misc_cov.ahk#L170)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Prevents the script from exiting automatically when its last thread completes, allowing it to stay running in an idle state.

## Syntax

````text
PrevSetting := Persistent(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("psetprio_own", () => ProcessSetPriority("Normal", ProcessExist()) > 0)
PersistFn() {
    Persistent()
    return 1
}
Check("persistent", PersistFn)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Persistent.htm](../../docs-v2/docs/lib/Persistent.htm)

````ahk
; This script will not exit automatically, even though it has nothing to do.
; However, you can use its tray icon to open the script in an editor, or to
; launch Window Spy or the Help file.
Persistent
````
