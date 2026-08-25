# CaretGetPos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:38](../../tests/doccheck/assert_sound_etc.ahk#L38)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the current position of the caret (text insertion point).

## Syntax

````text
CaretFound := CaretGetPos(&OutputVarX, &OutputVarY)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- CaretGetPos without a focused GTK window -------------------------------
x := y := "?"
ok := CaretGetPos(&x, &y)
Log("cgp=" (ok = 0 && x = "" && y = "" ? "ok" : "bad:" ok))

; --- CallbackCreate / CallbackFree ------------------------------------------
````

## Upstream reference example

Source: [docs-v2/docs/lib/CaretGetPos.htm](../../docs-v2/docs/lib/CaretGetPos.htm)

````ahk
SetTimer WatchCaret, 100
WatchCaret() {
    if CaretGetPos(&x, &y)
        ToolTip "X" x " Y" y, x, y - 20
    else
        ToolTip "No caret"
}
````
