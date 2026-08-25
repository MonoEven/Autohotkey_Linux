# SetTimer

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:94](../../tests/doccheck/assert_hotkey.ahk#L94)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_timer.ahk:18](../../tests/doccheck/assert_timer.ahk#L18)

Causes a function to be called automatically and repeatedly at a specified time interval.

## Syntax

````text
SetTimer Function, Period, Priority
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    n++
}
SetTimer(T, 60)
cnt6 := 0
CB6(ThisHotkey) {
    global cnt6
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetTimer.htm](../../docs-v2/docs/lib/SetTimer.htm)

````ahk
SetTimer CloseMailWarnings, 250
CloseMailWarnings()
{
    WinClose "Microsoft Outlook", "A timeout occured while communicating"
    WinClose "Microsoft Outlook", "A connection to the server could not be established"
}
````
