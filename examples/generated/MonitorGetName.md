# MonitorGetName

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:21](../../tests/doccheck/assert_monitor.ahk#L21)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the operating system's name of the specified monitor.

## Syntax

````text
Name := MonitorGetName(N)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("mon_count=" (MonitorGetCount() = 1 ? 1 : 0))
Log("mon_primary=" (MonitorGetPrimary() = 1 ? 1 : 0))
Log("mon_name=" (MonitorGetName(1) = "screen" ? 1 : 0))
MonitorGet(1, &ml, &mt, &mr, &mb)
Log("mon_get=" (ml = 0 && mt = 0 && mr = 1024 && mb = 768 ? 1 : 0))
Log("mon_get_ret=" (MonitorGet(1) = 1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/MonitorGetName.htm](../../docs-v2/docs/lib/MonitorGetName.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
