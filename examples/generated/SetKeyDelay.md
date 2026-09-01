# SetKeyDelay

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:140](../../tests/doccheck/assert_input.ahk#L140)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_sys.ahk:59](../../tests/doccheck/assert_sys.ahk#L59)

Sets the delay that will occur after each keystroke sent by Send or ControlSend.

## Syntax

````text
SetKeyDelay Delay, PressDuration, "Play"
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; (50ms) delay could collapse into one wake-up and flake; 150ms is measured
; robustly as >=120ms.
SetKeyDelay(150)
SendEvent("abc")
Sleep(600)
ts := all_key_times(next_lines())
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetKeyDelay.htm](../../docs-v2/docs/lib/SetKeyDelay.htm)

````ahk
SetKeyDelay 0
````
