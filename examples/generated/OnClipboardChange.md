# OnClipboardChange

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard_change.ahk:25](../../tests/doccheck/assert_clipboard_change.ahk#L25)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:229](../../tests/doccheck/assert_misc_cov.ahk#L229)

Registers a function to be called automatically whenever the clipboard's content changes.

## Syntax

````text
OnClipboardChange Callback , AddRemove
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
count1 := 0
count0 := 0
OnClipboardChange((type, *) => FileAppend("cb-" type "`n", CBFILE))
Log("ready=1")

; Stage A: the engine itself takes ownership (self-change also fires on
````

## Upstream reference example

Source: [docs-v2/docs/lib/OnClipboardChange.htm](../../docs-v2/docs/lib/OnClipboardChange.htm)

````ahk
OnClipboardChange ClipChanged
ClipChanged(DataType) {
    ToolTip "Clipboard data type: " DataType
    Sleep 1000
    ToolTip  ; Turn off the tip.
}
````
