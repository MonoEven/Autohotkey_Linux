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
    ; Linux Reload returns after spawning the replacement; terminate the old
    ; auto-execute thread immediately so it cannot consume the marker.
    ExitApp
````

## Upstream reference example

Source: [docs-v2/docs/lib/Reload.htm](../../docs-v2/docs/lib/Reload.htm)

````ahk
^!r::Reload  ; Ctrl+Alt+R
````
