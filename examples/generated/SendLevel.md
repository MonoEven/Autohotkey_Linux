# SendLevel

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotstring.ahk:34](../../tests/doccheck/assert_hotstring.ahk#L34)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_input.ahk:201](../../tests/doccheck/assert_input.ahk#L201)
- `x11`: [tests/doccheck/assert_inputhook.ahk:68](../../tests/doccheck/assert_inputhook.ahk#L68)
- `headless`: [tests/doccheck/assert_sys.ahk:108](../../tests/doccheck/assert_sys.ahk#L108)

Controls which artificial keyboard and mouse events are ignored by hotkeys and hotstrings.

## Syntax

````text
PrevLevel := SendLevel(Level)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Synthetic test input needs level > the default Hotstring input level 0.
; Auto-replacement itself is forced to level 0 and cannot recurse.
SendLevel(1)
Send("xq ")                 ; -> "ZZ" + space.
Send("cd ")                 ; -> "Q", space omitted.
Send("ef.")                 ; callback fires; '.' forwarded.
````

## Upstream reference example

Source: [docs-v2/docs/lib/SendLevel.htm](../../docs-v2/docs/lib/SendLevel.htm)

````ahk
SendLevel 1
SendEvent "btw{Space}" ; Produces "by the way ".
; This may be defined in a separate script:
::btw::by the way
````
