# Reload

- Linux status: `IMPL` (P1)
- Example kind: `lifecycle`
- Environment profile: `lifecycle`
- Verified source: [examples/lifecycle/reload_once.ahk:8](../../examples/lifecycle/reload_once.ahk#L8)
- Profile command: `bash examples/run.sh lifecycle "$BIN"`

Replaces the currently running instance of the script with a new one.

## Syntax

````text
Reload
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    FileAppend("first-instance`n", out)
    FileAppend("armed`n", marker)
    Reload
    ; The replacement starts asynchronously and then signals this process.
    ; Keep the old instance alive until that hand-off; reaching the line after
    ; Sleep means reload failed and is an explicit example failure.
````

## Upstream reference example

Source: [docs-v2/docs/lib/Reload.htm](../../docs-v2/docs/lib/Reload.htm)

````ahk
^!r::Reload  ; Ctrl+Alt+R
````
