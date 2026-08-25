# WinExist

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_unicode_lease.ahk:18](../../tests/doccheck/assert_unicode_lease.ahk#L18)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:26](../../tests/doccheck/assert_win.ahk#L26)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:239](../../tests/doccheck/assert_misc_cov.ahk#L239)

Checks if the specified window exists and returns the unique ID (HWND) of the first matching window.

## Syntax

````text
UniqueID := WinExist(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Run('out/xkeycap -out ' KCFILE ' -ms 120000')
WinWait("KeyCap Capture",, 5)
Log("step[winwait-rc]=" (WinExist("KeyCap Capture") ? 1 : 0))
Sleep(300)

; --- child script (a second AHK process on the same X server) ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinExist.htm](../../docs-v2/docs/lib/WinExist.htm)

````ahk
if WinExist("ahk_class Notepad") or WinExist("ahk_class" ClassName)
    WinActivate ; Use the window found by WinExist.
````
