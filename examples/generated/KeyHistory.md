# KeyHistory

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:41](../../tests/doccheck/assert_display.ahk#L41)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Displays script info and a history of the most recent keystrokes and mouse clicks.

## Syntax

````text
KeyHistory MaxEvents
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- KeyHistory: MaxEvents validated 0..500 (upstream), stored. ---
try
    KeyHistory(999)
catch ValueError
    Log("keyhistory_bad=1")
KeyHistory(100)
````

## Upstream reference example

Source: [docs-v2/docs/lib/KeyHistory.htm](../../docs-v2/docs/lib/KeyHistory.htm)

````ahk
KeyHistory
````
