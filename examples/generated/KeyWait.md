# KeyWait

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:283](../../tests/doccheck/assert_input.ahk#L283)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: XQueryKeymap polling; logical/physical state and debounce are approximate

Waits for a key or mouse/controller button to be released or pressed down.

## Syntax

````text
KeyWait KeyName , Options
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- KeyWait (docs: 1 when the condition is met; "D" waits for down). ---
Log("keywait_up=" (KeyWait("a") = 1 ? 1 : 0))
Send("{a down}")
Sleep(50)
Log("keywait_d=" (KeyWait("a", "D") = 1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/KeyWait.htm](../../docs-v2/docs/lib/KeyWait.htm)

````ahk
KeyWait "a"
````
