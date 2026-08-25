# PixelGetColor

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:39](../../tests/doccheck/assert_monitor.ahk#L39)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:113](../../tests/doccheck/assert_wayland.ahk#L113)

Retrieves the color of the pixel at the specified X and Y coordinates.

## Syntax

````text
Color := PixelGetColor(X, Y , Mode)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- PixelGetColor (docs: returns a hexadecimal numeric string). ---
Log("pix_black=" (PixelGetColor(5, 5) = "0x000000" ? 1 : 0))
Log("pix_fill=" (PixelGetColor(150, 150) = "0x336699" ? 1 : 0))
Log("pix_fill2=" (PixelGetColor(250, 150) = "0x3367A9" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/PixelGetColor.htm](../../docs-v2/docs/lib/PixelGetColor.htm)

````ahk
^!z::  ; Control+Alt+Z hotkey.
{
    MouseGetPos &MouseX, &MouseY
    MsgBox "The color at the current cursor position is " PixelGetColor(MouseX, MouseY)
}
````
