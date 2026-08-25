# WinActivate

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:59](../../tests/doccheck/assert_monitor.ahk#L59)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:152](../../tests/doccheck/assert_win.ahk#L152)

Activates the specified window.

## Syntax

````text
WinActivate WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- CoordMode Pixel: CLIENT = relative to the active window's client area. ---
CoordMode("Pixel", "Client")
WinActivate("MonWin")
Sleep(100)
Log("pix_client=" (PixelGetColor(55, 55) = "0x336699" ? 1 : 0))
CoordMode("Pixel", "Screen")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinActivate.htm](../../docs-v2/docs/lib/WinActivate.htm)

````ahk
if WinExist("Untitled - Notepad")
    WinActivate ; Use the window found by WinExist.
else
    WinActivate "Calculator"
````
