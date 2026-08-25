# Shutdown

- Linux status: `IMPL` (P1)
- Example kind: `safety-boundary`
- Environment profile: `safety-boundary`
- Verified source: [examples/safety/shutdown_requires_confirmation.ahk:10](../../examples/safety/shutdown_requires_confirmation.ahk#L10)
- Profile command: `"$BIN" examples/safety/shutdown_requires_confirmation.ahk`

Shuts down, restarts, or logs off the system.

## Syntax

````text
Shutdown Flags
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    ExitApp(2)
}
Shutdown 0
````

## Upstream reference example

Source: [docs-v2/docs/lib/Shutdown.htm](../../docs-v2/docs/lib/Shutdown.htm)

````ahk
Shutdown 6
````

## Safety boundary

The default example refuses the destructive operation. It requires an explicit acknowledgement argument and only demonstrates logoff; save all work before opting in.
