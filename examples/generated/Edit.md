# Edit

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:108](../../tests/doccheck/assert_edit.ahk#L108)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Opens the current script for editing in the default editor.

## Syntax

````text
Edit
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- Edit(): opens the script in $EDITOR (run_check.sh wrote the marker) ---
EnvSet("EDITOR", "/tmp/ahk_edit_marker.sh")
Edit()
Sleep(500)
if FileExist(MARKER) {
    content := FileRead(MARKER)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Edit.htm](../../docs-v2/docs/lib/Edit.htm)

````ahk
Edit
````
