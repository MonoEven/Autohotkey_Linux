# MonitorGetPrimary

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:20](../../tests/doccheck/assert_monitor.ahk#L20)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the number of the primary monitor.

## Syntax

````text
Primary := MonitorGetPrimary()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- Monitor* (XRandR: one output "screen", 1024x768). ---
Log("mon_count=" (MonitorGetCount() = 1 ? 1 : 0))
Log("mon_primary=" (MonitorGetPrimary() = 1 ? 1 : 0))
Log("mon_name=" (MonitorGetName(1) = "screen" ? 1 : 0))
MonitorGet(1, &ml, &mt, &mr, &mb)
Log("mon_get=" (ml = 0 && mt = 0 && mr = 1024 && mb = 768 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/MonitorGetPrimary.htm](../../docs-v2/docs/lib/MonitorGetPrimary.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
