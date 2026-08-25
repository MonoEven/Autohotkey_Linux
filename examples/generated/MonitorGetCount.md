# MonitorGetCount

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:19](../../tests/doccheck/assert_monitor.ahk#L19)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_statements.ahk:121](../../tests/doccheck/assert_statements.ahk#L121)

Returns the total number of monitors.

## Syntax

````text
Count := MonitorGetCount()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Monitor* (XRandR: one output "screen", 1024x768). ---
Log("mon_count=" (MonitorGetCount() = 1 ? 1 : 0))
Log("mon_primary=" (MonitorGetPrimary() = 1 ? 1 : 0))
Log("mon_name=" (MonitorGetName(1) = "screen" ? 1 : 0))
MonitorGet(1, &ml, &mt, &mr, &mb)
````

## Upstream reference example

Source: [docs-v2/docs/lib/MonitorGetCount.htm](../../docs-v2/docs/lib/MonitorGetCount.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
