# CoordMode

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_image.ahk:52](../../tests/doccheck/assert_image.ahk#L52)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_monitor.ahk:58](../../tests/doccheck/assert_monitor.ahk#L58)
- `headless`: [tests/doccheck/assert_sys.ahk:20](../../tests/doccheck/assert_sys.ahk#L20)

Sets coordinate mode for various built-in functions to be relative to either the active window or the screen.

## Syntax

````text
PrevRelativeTo := CoordMode(TargetType , RelativeTo)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; mode here before the first search (Xvfb is frameless and client coords
; coincide with screen coords, but sway/XWayland windows are offset).
CoordMode("Pixel", "Screen")
; Wait for the rectangle to be observable: sway/XWayland paints
; asynchronously (headless pixman renderer + wlr-screencopy can lag the
; window map), so a capture taken right after the map may still be blank.
````

## Upstream reference example

Source: [docs-v2/docs/lib/CoordMode.htm](../../docs-v2/docs/lib/CoordMode.htm)

````ahk
CoordMode "ToolTip", "Screen"
````
