# SysGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:180](../../tests/doccheck/assert_sys.ahk#L180)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Retrieves dimensions of system objects, and other system properties.

## Syntax

````text
Value := SysGet(Property)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Display-independent metrics (identical with or without an X display):
MsgBox "SysGet_buttons=" (SysGet(43) = 3)      ; SM_CMOUSEBUTTONS
MsgBox "SysGet_mouse=" (SysGet(19) = 1)        ; SM_MOUSEPRESENT
MsgBox "SysGet_network=" (SysGet(63) = 1)      ; SM_NETWORK
MsgBox "SysGet_cleanboot=" (SysGet(67) = 0)    ; SM_CLEANBOOT
````

## Upstream reference example

Source: [docs-v2/docs/lib/SysGet.htm](../../docs-v2/docs/lib/SysGet.htm)

````ahk
MouseButtonCount := SysGet(43)
````
