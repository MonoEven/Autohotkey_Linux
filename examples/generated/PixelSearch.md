# PixelSearch

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:44](../../tests/doccheck/assert_monitor.ahk#L44)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Searches a region of the screen for a pixel of the specified color.

## Syntax

````text
PixelSearch &OutputVarX, &OutputVarY, X1, Y1, X2, Y2, ColorID , Variation
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- PixelSearch (docs: 1/0; outputs blank when not found; variation). ---
found := PixelSearch(&px, &py, 140, 140, 160, 160, 0x336699)
Log("search_found=" (found = 1 && px = 140 && py = 140 ? 1 : 0))
found2 := PixelSearch(&px2, &py2, 0, 0, 50, 50, 0x336699)
Log("search_miss=" (found2 = 0 && px2 = "" && py2 = "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/PixelSearch.htm](../../docs-v2/docs/lib/PixelSearch.htm)

````ahk
if PixelSearch(&Px, &Py, 200, 200, 300, 300, 0x9d6346, 3)
    MsgBox "A color within 3 shades of variation was found at X" Px " Y" Py
else
    MsgBox "That color was not found in the specified region."
````
